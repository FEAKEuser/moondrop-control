#!/usr/bin/env python3
"""Minimal OCS v1 provider, so the KDE Store channel can be tested locally.

KNewStuff (what "Get New Widgets" is built on) talks to a provider over the OCS
v1 API.  Standing a provider up on localhost is the only way to exercise the real
download-and-install code path without publishing anything, and it is exactly the
endpoint the shipped `.plasmoid` will travel through.

The response shape matches what api.kde-look.org returns (verified against the
live service): a flat object with status/statuscode/data, and one entry per
content item carrying downloadlink1 / downloadname1 / xdg_type.

    python3 tools/ocs-provider.py --port 8099 --plasmoid dist/org.moondrop.control-0.1.0.plasmoid

Then point a throwaway KNS config at it:

    [KNewStuff]
    Name=Plasma Widgets (local test)
    Categories=Plasma 6 Extensions
    ProvidersUrl=file:///tmp/ocs/providers.xml
    StandardResource=tmp
    Uncompress=kpackage
    KPackageStructure=Plasma/Applet
"""
import argparse
import json
import os
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from xml.sax.saxutils import escape, quoteattr

ARGS = None

# KNewStuff's OCS client speaks XML by default (Attica parses the response with
# QXmlStreamReader); `format=json` is only used when the caller asks for it.  The
# live service answers both, so this provider does too - serving only JSON makes
# the client fail with "XML Error: Start tag expected".
CONTENT_FIELDS = [
    "id", "name", "version", "typeid", "typename", "xdg_type", "language",
    "personid", "created", "changed", "downloads", "score", "summary",
    "description", "comments", "ghns_excluded", "preview1", "detailpage", "tags",
    "previewpic1", "smallpreviewpic1", "downloadway1", "downloadtype1",
    "downloadprice1", "downloadlink1", "downloadname1", "downloadsize1",
    "downloadgpgfingerprint1", "downloadgpgsignature1", "downloadpackagename1",
    "downloadrepository1", "download_package_type1",
]


def entry(plasmoid_path, base_url):
    name = os.path.basename(plasmoid_path)
    size = os.path.getsize(plasmoid_path) // 1024
    return {
        "details": "summary",
        "id": 1,
        "name": "Moondrop Control",
        "version": "0.1.0",
        "typeid": 706,
        "typename": "Plasma 6 Applets",
        "xdg_type": "plasma6_plasmoids",
        "language": "",
        "personid": "FEAKEuser",
        "created": "2026-09-15T00:00:00+00:00",
        "changed": "2026-09-15T00:00:00+00:00",
        "downloads": "0",
        "score": 50,
        "summary": "Control MOONDROP headphones: noise cancelling, EQ presets, LDAC and battery",
        "description": "<p>A KDE Plasma applet to control MOONDROP Bluetooth headphones.</p>",
        "comments": 0,
        "ghns_excluded": 0,
        "preview1": base_url + "/p/1",
        "detailpage": base_url + "/p/1",
        "tags": "headphones,bluetooth,plasma6",
        "previewpic1": base_url + "/preview.png",
        "smallpreviewpic1": base_url + "/preview.png",
        "downloadway1": 1,
        "downloadtype1": "",
        "downloadprice1": 0,
        # the download the KNS client follows
        "downloadlink1": base_url + "/download/" + urllib.parse.quote(name),
        "downloadname1": name,
        "downloadsize1": size,
        "downloadgpgfingerprint1": "",
        "downloadgpgsignature1": "",
        "downloadpackagename1": "",
        "downloadrepository1": "",
        "download_package_type1": "",
    }


def xml_response(meta, items, item_tag, fields):
    """Serialize an OCS response the way api.kde-look.org does."""
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<ocs>", "<meta>"]
    out.append("<status>%s</status>" % escape(meta["status"]))
    out.append("<statuscode>%d</statuscode>" % meta["statuscode"])
    out.append("<message>%s</message>" % escape(meta.get("message", "")))
    out.append("<totalitems>%d</totalitems>" % meta.get("totalitems", len(items)))
    if "itemsperpage" in meta:
        out.append("<itemsperpage>%d</itemsperpage>" % meta["itemsperpage"])
    out.append("</meta>")
    out.append('<data details="summary">' if item_tag == "content" else "<data>")
    for item in items:
        out.append("<%s>" % item_tag)
        for f in fields:
            if f not in item:
                continue
            val = str(item[f])
            if f == "description":
                out.append("<%s><![CDATA[%s]]></%s>" % (f, val, f))
            else:
                out.append("<%s>%s</%s>" % (f, escape(val), f))
        out.append("</%s>" % item_tag)
    out.append("</data>")
    out.append("</ocs>")
    return "".join(out).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        sys.stderr.write("[ocs] %s\n" % (fmt % a))

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _ocs(self, meta, items, item_tag, fields=None):
        """Answer in the format the caller asked for; XML is the OCS default."""
        wants_json = ARGS.force_json or \
            self.path.lower().find("format=json") >= 0
        if wants_json:
            body = dict(meta)
            body["data"] = items
            self._send(200, json.dumps(body).encode(), "application/json")
        else:
            self._send(200, xml_response(meta, items, item_tag, fields or []),
                       "application/xml; charset=utf-8")

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        base = "http://%s" % self.headers.get("Host", "127.0.0.1")

        if path.endswith("/content/data") or path.endswith("/content/data/"):
            self._ocs(
                {"status": "ok", "statuscode": 100, "message": "",
                 "totalitems": 1, "itemsperpage": 10},
                [entry(ARGS.plasmoid, base)], "content", CONTENT_FIELDS)
            return

        # The KNS client asks for the category list before searching.  The shape
        # and the ids below mirror api.kde-look.org: knsrc's `Categories` entry
        # names a parent category ("Plasma 6 Extensions", 705) whose children
        # carry the xdg_type the client filters on ("plasma6_plasmoids").
        if path.endswith("/content/categories") or path.endswith("/content/categories/"):
            cats = [
                {"id": 705, "name": "Plasma 6 Extensions",
                 "display_name": "Plasma 6 Extensions", "parent_id": "", "xdg_type": ""},
                {"id": 706, "name": "Plasma 6 Applets",
                 "display_name": "Plasma 6 Applets", "parent_id": "705",
                 "xdg_type": "plasma6_plasmoids"},
            ]
            self._ocs(
                {"status": "ok", "statuscode": 100, "message": "",
                 "totalitems": len(cats), "itemsperpage": 100},
                cats, "category",
                ["id", "name", "display_name", "parent_id", "xdg_type"])
            return

        # `content/download/<contentid>/<linkid>` is how the client resolves a
        # downloadlink before fetching it: the search result carries a
        # downloadway (1 = direct link, 2 = this endpoint), and for 2 the client
        # asks here for the real URL.  api.kde-look.org answers with
        # <downloadlink>/<mimetype>/<gpgfingerprint> under details="download".
        if "/content/download/" in path:
            parts = [p for p in path.split("/") if p]
            linkid = parts[-2] if len(parts) >= 2 and parts[-1].isdigit() else "1"
            real_name = os.path.basename(ARGS.plasmoid)
            info = {
                "downloadway": 1,
                "downloadlink": base + "/download/" + urllib.parse.quote(real_name),
                "mimetype": "application/zip",
                "gpgfingerprint": "",
                "gpgsignature": "",
                "packagename": "",
                "repository": "",
                "size": os.path.getsize(ARGS.plasmoid) // 1024,
            }
            del linkid
            self._ocs({"status": "ok", "statuscode": 100, "message": ""},
                      [info], "content",
                      ["downloadway", "downloadlink", "mimetype", "gpgfingerprint",
                       "gpgsignature", "packagename", "repository", "size"])
            return

        if path.startswith("/download/"):
            name = urllib.parse.unquote(os.path.basename(path))
            real = os.path.basename(ARGS.plasmoid)
            if name != real:
                self._send(404, b"no such file", "text/plain")
                return
            with open(ARGS.plasmoid, "rb") as fh:
                self._send(200, fh.read(), "application/octet-stream")
            return

        if path.endswith("/preview.png"):
            # a 1x1 PNG is enough for the client's preview loader
            png = bytes.fromhex(
                "89504e470d0a1a0a0000000d494844520000000100000001080600000"
                "01f15c4890000000a49444154789c6300010000050001"
                "0d0a2db40000000049454e44ae426082"
            )
            self._send(200, png, "image/png")
            return

        self._send(404, b"not found", "text/plain")


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--plasmoid", required=True)
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--force-json", action="store_true",
                    help="answer in JSON even when the client did not ask for it")
    ARGS = ap.parse_args()
    if not os.path.isfile(ARGS.plasmoid):
        sys.exit("no such plasmoid: %s" % ARGS.plasmoid)
    srv = ThreadingHTTPServer(("127.0.0.1", ARGS.port), Handler)
    sys.stderr.write("[ocs] serving %s on http://127.0.0.1:%d\n" % (ARGS.plasmoid, ARGS.port))
    srv.serve_forever()


if __name__ == "__main__":
    main()
