%{?rhel:%global centos_ver %rhel}

Name:           netdata-repo
Version:        1
Release:        1
Summary:        Netdata stable repositories configuration.

Group:          System Environment/Base
License:        GPLv2

Source0:        netdata.repo.fedora
Source1:        netdata-edge.repo.fedora
Source2:        netdata.repo.suse
Source3:        netdata-edge.repo.suse
Source4:        netdata.repo.centos
Source5:        netdata-edge.repo.centos

BuildArch:      noarch

%description
This package contains the official Netdata package repository configuration for stable versions of Netdata.

%prep
%setup -q  -c -T

%if 0%{?fedora}
install -pm 644 %{SOURCE0} ./netdata.repo
install -pm 644 %{SOURCE1} ./netdata-edge.repo
%endif

%if 0%{?suse_version}
install -pm 644 %{SOURCE2} ./netdata.repo
install -pm 644 %{SOURCE3} ./netdata-edge.repo
%endif

%if 0%{?centos_ver}
install -pm 644 %{SOURCE4} ./netdata.repo
install -pm 644 %{SOURCE5} ./netdata-edge.repo
%endif

%build
true

%install
rm -rf $RPM_BUILD_ROOT

%if 0%{?suse_version}
install -dm 755 $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
install -pm 644 netdata.repo $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
install -pm 644 netdata-edge.repo $RPM_BUILD_ROOT%{_sysconfdir}/zypp/repos.d
%else
install -dm 755 $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d
install -pm 644 netdata.repo $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d
install -pm 644 netdata-edge.repo $RPM_BUILD_ROOT%{_sysconfdir}/yum.repos.d
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%files
%if 0%{?suse_version}
%attr(644,root,root) /etc/zypp/repos.d/netdata.repo
%else
%attr(644,root,root) /etc/yum.repos.d/netdata.repo
%endif

%package edge
Summary: Netdata nightly repositories configuration.
Group:   System Environment/Base

%description edge
This package contains the official Netdata package repository configuration for nightly versions of Netdata.

%files edge
%if 0%{?suse_version}
%attr(644,root,root) /etc/zypp/repos.d/netdata-edge.repo
%else
%attr(644,root,root) /etc/yum.repos.d/netdata-edge.repo
%endif

%changelog
* Mon Jun 14 2021 Austin Hemmelgarn <austin@netdata.cloud> 1-1
- Initial revision
