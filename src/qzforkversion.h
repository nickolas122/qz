#ifndef QZFORKVERSION_H
#define QZFORKVERSION_H

/*
 * The fork's own release identity.
 *
 * Qt's VERSION in qdomyos-zwift.pri has to stay numeric - qmake turns it into the Windows
 * file version resource, which is what QCoreApplication::applicationVersion() reports - so
 * the fork's release number lives here instead, where it can carry a suffix.
 *
 * The base is the upstream version this fork was built from, and the -qz.N counter bumps
 * once per release of this fork. The base does not move: this fork deletes upstream code
 * rather than carrying patches on top of it, so there is no rebase to move it, and the
 * counter never resets. See FORK.md, Versioning.
 *
 * The release workflow refuses to publish if the tag and this string disagree, so bump
 * this in the same commit that gets tagged.
 */
#define QZ_FORK_VERSION "2.21.6-qz.4"

#endif // QZFORKVERSION_H
