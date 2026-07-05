#pragma once

#include <QFile>
#include <QString>
#include <QTextStream>
#include <QDateTime>

class Logger {
public:
    enum class Level {
        Debug,
        Info,
        Warning,
        Error
    };

    static Logger& instance();

    void setLogFile(const QString& filePath);
    void setLevel(Level level);
    void log(Level level, const QString& message);

    void debug(const QString& message) { log(Level::Debug, message); }
    void info(const QString& message) { log(Level::Info, message); }
    void warning(const QString& message) { log(Level::Warning, message); }
    void error(const QString& message) { log(Level::Error, message); }

private:
    Logger() = default;
    ~Logger();

    QFile logFile_;
    QTextStream stream_;
    Level minLevel_ = Level::Info;

    QString levelToString(Level level) const;
};

// 便捷宏
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
