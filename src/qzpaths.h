#ifndef QZPATHS_H
#define QZPATHS_H

#include <QString>
#include <QUrl>

/**
 * @brief Where QZ is allowed to write, and how a saved settings file gets read back.
 *
 * These seven were statics on homeform, which made them unreachable from anything that
 * is not the old UI - except that main.cpp calls three of them from argument parsing,
 * long before any UI exists. That is the contradiction 7c-2 had to resolve: the class
 * being deleted owned the answer to "where does this platform let QZ write", and the
 * answer is needed whether or not there is a UI at all.
 *
 * Nothing here is UI. Profile loading rides along because it is nothing more than
 * reading a file out of one of these directories into QSettings.
 *
 * See STRIP-SPEC.md section 7 group F.
 */
class QzPaths {
  public:
    /** @brief The directory QZ may write to on this platform. Trailing separator. */
    static QString getWritableAppDir();

    /** @brief Where saved .qzs settings profiles live. Created if absent. */
    static QString getProfileDir();

    /** @brief Android's per-app external files directory. Empty elsewhere. */
    static QString getAndroidDataAppDir();

    /** @brief Key for the encrypted fields in a saved profile, generated once. */
    static quint64 cryptoKeySettingsProfiles();

    static QString getFileNameFromContentUri(const QString &uriString);

    /** @brief Copy an Android content:// URI into a QZ subfolder, returning the path. */
    static QString copyAndroidContentsURI(QUrl file, QString subfolder);

    /** @brief Apply a saved .qzs profile to QSettings, decrypting what is encrypted. */
    static void loadSettings(const QUrl &filename);
};

#endif // QZPATHS_H
