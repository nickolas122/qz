#ifndef QTOAUTHCOMPAT_H
#define QTOAUTHCOMPAT_H

#include <QtGlobal>
#include <QString>
#include <QVariant>

/*
 * QAbstractOAuth::ModifyParametersFunction takes a QVariantMap* on Qt 5 and a
 * QMultiMap<QString, QVariant>* on Qt 6. A generic lambda absorbs the type
 * change, but not the semantics: QMultiMap::insert() appends a second value for
 * a key that already exists rather than replacing it, and QMultiMap has no
 * operator[] at all. This sets a parameter to exactly one value on both.
 */
template <typename Map> inline void qzOAuthSetParameter(Map *parameters, const QString &key, const QVariant &value) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    parameters->replace(key, value);
#else
    parameters->insert(key, value);
#endif
}

#endif // QTOAUTHCOMPAT_H
