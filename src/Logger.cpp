#include "Logger.h"

#include <QDir>
#include <QStandardPaths>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    if (logFile_.isOpen()) {
        stream_.flush();
        logFile_.close();
    }
}

void Logger::setLogFile(const QString& filePath)
{
    if (logFile_.isOpen()) {
        stream_.flush();
        logFile_.close();
    }

    logFile_.setFileName(filePath);
    if (logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        stream_.setDevice(&logFile_);
    }
}

void Logger::setLevel(Level level)
{
    minLevel_ = level;
}

void Logger::log(Level level, const QString& message)
{
    if (level < minLevel_) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString levelStr = levelToString(level);
    const QString logLine = QStringLiteral("[%1] [%2] %3\n").arg(timestamp, levelStr, message);

    if (stream_.device() != nullptr) {
        stream_ << logLine;
        stream_.flush();
    }

    // 也输出到控制台（调试模式）
#ifdef QT_DEBUG
    fprintf(stderr, "%s", logLine.toUtf8().constData());
#endif
}

QString Logger::levelToString(Level level) const
{
    switch (level) {
    case Level::Debug:   return QStringLiteral("DEBUG");
    case Level::Info:    return QStringLiteral("INFO");
    case Level::Warning: return QStringLiteral("WARNING");
    case Level::Error:   return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}
