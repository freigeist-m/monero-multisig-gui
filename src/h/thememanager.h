#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QQmlEngine>

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString surfaceColor READ surfaceColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString textColor READ textColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString textSecondaryColor READ textSecondaryColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString iconColor READ iconColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY darkModeChanged)
    Q_PROPERTY(QString accentColor READ accentColor CONSTANT)
    Q_PROPERTY(QString primaryColor READ primaryColor CONSTANT)
    Q_PROPERTY(QString successColor READ successColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString warningColor READ warningColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString errorColor READ errorColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString borderColor READ borderColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString hoverColor READ hoverColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString pressedColor READ pressedColor NOTIFY darkModeChanged)
    Q_PROPERTY(QString disabledColor READ disabledColor NOTIFY darkModeChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)


public:
    explicit ThemeManager(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }

    QString backgroundColor() const;
    QString surfaceColor() const;
    QString textColor() const;
    QString textSecondaryColor() const;
    QString iconColor() const;
    QString iconPath() const;
    QString accentColor() const { return "#2196F3"; }
    QString primaryColor() const { return "#1976D2"; }
    QString successColor() const;
    QString warningColor() const;
    QString errorColor() const;
    QString borderColor() const;
    QString hoverColor() const;
    QString pressedColor() const;
    QString disabledColor() const;


    Q_INVOKABLE void toggleTheme();

public slots:
    void setDarkMode(bool dark);

signals:
    void darkModeChanged();

private:
    bool m_darkMode = true;
};

#endif
