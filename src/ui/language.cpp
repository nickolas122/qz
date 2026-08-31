#include "language.h"

#include "qzsettings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

namespace {

/** Turns a stored setting into a locale name: "auto" asks the system, the rest are literal. */
QString resolveLocale(const QString &code) {
    const QString stored = code.trimmed();
    if (stored.isEmpty() || stored.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        return QLocale::system().name();
    }
    QString locale = stored;
    locale.replace(QLatin1Char('-'), QLatin1Char('_'));
    return locale;
}

} // namespace

QzLanguage::QzLanguage(QQmlEngine *engine, QObject *parent) : QObject(parent), m_engine(engine) {
    QSettings settings;
    m_current = settings.value(QZSettings::app_language, QZSettings::default_app_language).toString().trimmed();
    if (m_current.isEmpty()) {
        m_current = QZSettings::default_app_language;
    }
    apply(m_current);
}

QStringList QzLanguage::codes() const {
    return {QStringLiteral("auto"), QStringLiteral("pt_BR"), QStringLiteral("en")};
}

QStringList QzLanguage::names() const {
    // The two language names are written in their own language and stay that way: a
    // rider who has landed in the wrong one has to be able to read their way out of it.
    // "System" is not a language name, so it is translated like any other label.
    return {tr("System"), QStringLiteral("Português"), QStringLiteral("English")};
}

void QzLanguage::setCurrent(const QString &code) {
    if (code == m_current) {
        return;
    }
    m_current = code;
    QSettings settings;
    settings.setValue(QZSettings::app_language, m_current);
    apply(m_current);
    emit currentChanged();

    // Re-evaluates every qsTr() binding in the loaded tree. Without it the new catalogue
    // is installed and nothing on screen moves until the next launch.
    if (m_engine) {
        m_engine->retranslate();
    }
}

void QzLanguage::apply(const QString &code) {
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    const QString locale = resolveLocale(code);

    // English is the source language, so there is no catalogue to load and nothing to
    // fail: the qsTr() strings in the .qml files are already English.
    if (locale.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
        qDebug() << QStringLiteral("Language") << code << QStringLiteral("resolves to English (source strings)");
        return;
    }

    // Any flavour of Portuguese gets the Brazilian catalogue - pt_PT is closer to it
    // than English is. Everything else falls through to the source strings, which is
    // the honest answer for a fork that ships two languages.
    auto *translator = new QTranslator(this);
    const QString base = QStringLiteral(":/translations/translations/qdomyos-zwift_");
    bool loaded = translator->load(base + locale);
    if (!loaded && locale.startsWith(QStringLiteral("pt"), Qt::CaseInsensitive)) {
        loaded = translator->load(base + QStringLiteral("pt_BR"));
    }

    if (!loaded) {
        qDebug() << QStringLiteral("No catalogue for locale") << locale << QStringLiteral("- using English");
        delete translator;
        return;
    }

    QCoreApplication::installTranslator(translator);
    m_translator = translator;
    qDebug() << QStringLiteral("Language") << code << QStringLiteral("loaded catalogue for") << locale;
}
