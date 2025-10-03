#include "thememanager.h"

ThemeManager::ThemeManager(QObject *parent) : QObject(parent) {}

void ThemeManager::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        emit darkModeChanged();
    }
}

QString ThemeManager::backgroundColor() const
{
    return m_darkMode ? "#1e1e1e" : "#ffffff";
}

QString ThemeManager::surfaceColor() const
{
    return m_darkMode ? "#2d2d2d" : "#f8f9fa";
}

QString ThemeManager::textColor() const
{
    return m_darkMode ? "#ffffff" : "#000000";
}

QString ThemeManager::textSecondaryColor() const
{
    return m_darkMode ? "#b0b0b0" : "#666666";
}

QString ThemeManager::iconColor() const
{
    return m_darkMode ? "#ffffff" : "#666666";
}

QString ThemeManager::iconPath() const
{
    return "qrc:/resources/icons/";
}

QString ThemeManager::successColor() const
{
    return m_darkMode ? "#4caf50" : "#2e7d32";
}

QString ThemeManager::warningColor() const
{
    return m_darkMode ? "#ff9800" : "#f57c00";
}

QString ThemeManager::errorColor() const
{
    return m_darkMode ? "#f44336" : "#d32f2f";
}

QString ThemeManager::borderColor() const
{
    return m_darkMode ? "#404040" : "#e0e0e0";
}

QString ThemeManager::hoverColor() const
{
    return m_darkMode ? "#3a3a3a" : "#f5f5f5";
}

QString ThemeManager::pressedColor() const
{
    return m_darkMode ? "#505050" : "#eeeeee";
}

QString ThemeManager::disabledColor() const
{
    return m_darkMode ? "#808080" : "#bdbdbd";
}

void ThemeManager::toggleTheme()
{
    setDarkMode(!m_darkMode);
}
