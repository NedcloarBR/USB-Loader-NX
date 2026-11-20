## What this changes

<!-- What the change does and why. Link the issue it closes: "Closes #123". -->

## How it was tested

<!-- Console, firmware, emuMMC or sysMMC, the games and drives used. Code that runs on the console has to be tested on one. -->

- Console:
- Firmware:
- Tested:

## Checklist

- [ ] Commits follow [Conventional Commits](https://www.conventionalcommits.org) (`feat(manager): ...`, `fix(usbl): ...`)
- [ ] Builds with `scripts/build.sh` (`atmosphere`, `manager`, or both)
- [ ] Changes to Atmosphère itself (`atmosphere/`) are separate commits for the `atmosphere` branch
- [ ] Code is my own or GPLv2-compatible, and nothing is copied from closed-source projects
- [ ] README, `manager/README.md` or `TODO.md` updated when the behaviour or an option changed
- [ ] Anything that writes to the system (ncm, ns, es, saves, settings) or adds a service is described above
