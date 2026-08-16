#ifndef QZFORKVERSION_H
#define QZFORKVERSION_H

/*
 * The fork's own release identity.
 *
 * Qt's VERSION in qdomyos-zwift.pri has to stay numeric - qmake turns it into the Windows
 * file version resource, which is what QCoreApplication::applicationVersion() reports - so
 * the fork's release number lives here instead, where it can carry a suffix.
 *
 * The base is the upstream version this fork is built from, and the -qz.N counter bumps
 * once per release of this fork. Rebasing on a newer upstream moves the base and resets
 * the counter to 1.
 *
 * The release workflow refuses to publish if the tag and this string disagree, so bump
 * this in the same commit that gets tagged.
 */
#define QZ_FORK_VERSION "2.21.6-qz.1"

#endif // QZFORKVERSION_H
