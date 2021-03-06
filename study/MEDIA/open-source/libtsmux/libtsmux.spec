Name:		libtsmux
Version:	0.3.0
Release:	1
Summary:	Library for muxing of MPEG Transport Streams
Group:		Applications/Multimedia
License:	LGPL/GPL/MPL/MIT
URL:		http://schrodinger.sf.net
Source:		libtsmux-%{version}.tar.bz2
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
Library for muxing of MPEG Transport Streams

%package devel
Summary: Libraries and includefiles for libtsmux
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
Header files and libraries for libtsmux

%prep
%setup -q -n %{name}-%{version}

%build
%configure
make

%install
rm -rf $RPM_BUILD_ROOT
%makeinstall

# no static libs and libtool archives either
rm -f $RPM_BUILD_ROOT%{_libdir}/*.a
rm -f $RPM_BUILD_ROOT%{_libdir}/*.la

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc README NEWS COPYING COPYING.GPL COPYING.LGPL COPYING.MIT COPYING.MPL
%{_libdir}/libtsmux.so.*

%files devel
%defattr(-,root,root)
%{_includedir}/tsmux/tsmux.h
%{_includedir}/tsmux/tsmuxcommon.h
%{_includedir}/tsmux/tsmuxstream.h
%{_libdir}/pkgconfig/tsmux.pc
%{_libdir}/libtsmux.so



%changelog
* Mon Nov 13 2006 Christian F.K. Schaller <christian@fluendo.com>
- First attempt at spec
