# jiralike-demo Ubuntu / Debian Packaging

Builds three `.deb` packages:
- `jiralike-demo_0.1.deb`: FastCGI C++23 backend, SQLite migration scripts (`db/migrations/`), and systemd service.
- `jiralike-demo-fe_0.1.deb`: Angular SPA production frontend bundle.
- `jiralike-demo-config-nginx_0.1.deb`: Nginx virtual host configuration.

```bash
./build-all.sh
sudo dpkg -i jiralike-demo_0.1.deb jiralike-demo-fe_0.1.deb jiralike-demo-config-nginx_0.1.deb
```
