<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# wimsy-core documentation

New here? [Install](./INSTALL.md) sets the services up on FreeBSD from nothing; each program's page says what it does,
its settings and its service.

## Running the services

- [Install](./INSTALL.md): the host and its tools, the jails, the packages, each service and its Discord application,
  upgrades and removal, and what another machine changes
- [Security](./SECURITY.md): the secrets and where each lives, how little each token may do, what each jail reaches,
  Tracer's workers, the site's and the MCP server's limits, and what the bots keep of people

## The programs

- [ReiWimsy](./REIWIMSY.md): the replies and the cards, their settings and service, what it keeps, why it stops
- [Tracer](./TRACER.md): the reports and their commands, issues, how old a build is, what it keeps of people, the site,
  its settings and service
- [tracer-mcp](./TRACER-MCP.md): the reports for agents: its tools, limits, settings and service
- [AccordWimsy](./ACCORDWIMSY.md): what a pass mirrors and what not, its state and logs, its commands and settings

## The pages

- [The forge](./FORGE.md): its three jails, the guard, Forgejo's settings, the start page and AccordWimsy's jail
- [The status page](./STATUS.md): what it checks, its files, putting it up and changing it
- [Themes](./THEMES.md): SWS's themes on the three sites, where each piece lives, the picker, adding a theme

## Development

- [Build](./BUILD.md): from source on FreeBSD and Linux, Concord, and the FreeBSD packages
- [Development](./DEVELOPMENT.md): the code's layout, its rules, the pieces from elsewhere and how each is updated, CI
- [Testing](./TESTING.md): what each suite covers, the docs' check, the bots against a fake Discord, the status page

## License

- [Licenses and credits](./CREDITS.md)
- [GPL-3.0-or-later](../LICENSE)
