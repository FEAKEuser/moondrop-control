# Fedora / COPR spec for Moondrop Control.
#
# Build locally:
#     rpmbuild -bb packaging/fedora/moondrop-control.spec \
#         --define "_sourcedir $(pwd)" --define "_specdir $(pwd)"
#
# or on COPR (https://copr.fedorainfracloud.org), which builds from a git URL.
#
# Layout note: the applet package and the C++ QML plugin go to different
# prefixes.  kpackagetool6 is not run by the package: a system wide package just
# has to land in the shared location Plasma already searches
# (/usr/share/plasma/plasmoids), and the QML plugin has to land in Qt's import
# path (/usr/lib64/qt6/qml), which is why there are two subpackages worth of
# files but only one package - splitting them would let a user end up with a
# widget whose backend is missing.

%global debug_package %{nil}

Name:           moondrop-control
Version:        0.3.2
Release:        1%{?dist}
Summary:        KDE Plasma applet to control MOONDROP Bluetooth headphones

License:        GPL-3.0-or-later
URL:            https://github.com/FEAKEuser/moondrop-control
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  extra-cmake-modules
BuildRequires:  kf6-ki18n-devel
BuildRequires:  gettext
BuildRequires:  kf6-rpm-macros

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       kf6-ki18n
Requires:       bluez
Requires:       plasma-workspace

%description
A KDE Plasma 6 widget (and a command line tool) that controls MOONDROP
Bluetooth headphones over the RFCOMM control channel: noise cancelling modes,
tuning presets, a multi-band parametric EQ, LDAC/LC3/LHDC switches, DAC gain,
multipoint and battery levels.

The applet talks to the headphone directly through an AF_BLUETOOTH socket as
the ordinary user; it needs neither root nor the bluetoothctl command line.

Verified on MOONDROP EDGE (firmware 1.4.0) and Moondrop Nekocake (firmware
1.0.0); profiles for other models are written from public protocol
documentation and are marked as unverified in the user interface.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake_kf6 -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

# The applet package must be visible to every user without running
# kpackagetool6 per account, so put a copy under the shared plasmoid directory.
mkdir -p %{buildroot}%{_datadir}/plasma/plasmoids
cp -a package %{buildroot}%{_datadir}/plasma/plasmoids/org.moondrop.control

%files
%license LICENSE
%doc README.md DEVELOPMENT.md docs/PROTOCOL.md
%{_bindir}/moondrop-cli
%{_bindir}/moondrop-widget-install-applet
%{_bindir}/moondrop-widget-uninstall
%{_datadir}/moondrop-widget/
%{_datadir}/metainfo/org.moondrop.control.metainfo.xml
%{_datadir}/plasma/plasmoids/org.moondrop.control/
%dir %{_qt6_qmldir}/org/moondrop
%{_qt6_qmldir}/org/moondrop/backend/

%changelog
* Sat Nov 14 2026 FEAKEuser <feohz@users.noreply.github.com> - 0.3.2-1
- Build the Fedora, Debian and Arch packages automatically on release tags

* Sat Nov 14 2026 FEAKEuser <feohz@users.noreply.github.com> - 0.3.1-1
- Noise-cancelling tab icon follows the current mode; ANC uses the noise-reduction icon

* Sat Nov 14 2026 FEAKEuser <feohz@users.noreply.github.com> - 0.3.0-1
- Device catalog from the official app, N-band parametric EQ, extra settings and gestures

* Tue Nov 10 2026 FEAKEuser <feohz@users.noreply.github.com> - 0.2.0-1
- Remember and restore the noise cancelling mode
- Make the settings page controls actually change the settings

* Tue Sep 15 2026 FEAKEuser <feohz@users.noreply.github.com> - 0.1.0-1
- Initial package
