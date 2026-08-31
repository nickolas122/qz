#ifndef LANGUAGE_H
#define LANGUAGE_H

#include <QObject>
#include <QString>
#include <QStringList>

class QQmlEngine;
class QTranslator;

/**
 * @brief The app's language, and the one thing that can change it.
 *
 * Two catalogues are read here and only two are shipped: English, which is the source
 * language every qsTr() in src/ui is written in and therefore has no .qm at all, and
 * Brazilian Portuguese. "auto" means whatever QLocale::system() says, which resolves to
 * one of those two.
 *
 * It is a QObject rather than the twenty lines of QTranslator setup this replaces in
 * main.cpp because the setting has to be changeable from the Settings screen, and a
 * language that only takes effect on the next launch is the kind of half-answer section
 * 3.2.1 exists to prevent. Swapping the translator and calling QQmlEngine::retranslate()
 * re-evaluates every qsTr() binding in the tree, so the switch lands while the rider is
 * looking at it.
 */
class QzLanguage : public QObject {
    Q_OBJECT

    /** "auto", "pt_BR" or "en" - the setting as stored, not as resolved. */
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)

    /** The codes above, in the order the picker shows them. */
    Q_PROPERTY(QStringList codes READ codes CONSTANT)

    /** What each of those codes is called, in its own language. */
    Q_PROPERTY(QStringList names READ names CONSTANT)

  public:
    /**
     * @brief Reads the stored setting and installs its catalogue.
     *
     * Construct this before QQmlApplicationEngine::load(), or the first tree is built
     * against the source strings and only redrawn on the first change.
     */
    explicit QzLanguage(QQmlEngine *engine, QObject *parent = nullptr);

    QString current() const { return m_current; }
    QStringList codes() const;
    QStringList names() const;

    void setCurrent(const QString &code);

  signals:
    void currentChanged();

  private:
    /** Installs the catalogue for a stored code, English meaning "install nothing". */
    void apply(const QString &code);

    QQmlEngine *m_engine;
    QTranslator *m_translator = nullptr;
    QString m_current;
};

#endif // LANGUAGE_H
