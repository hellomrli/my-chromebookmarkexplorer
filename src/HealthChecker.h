#pragma once

#include "BookmarkNode.h"

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>

struct HealthResult {
    QString url;
    QString status;
    int code = 0;
    int elapsedMs = 0;
    QString finalUrl;
    QString error;

    bool ok() const;
};

class HealthChecker : public QObject {
    Q_OBJECT

public:
    explicit HealthChecker(QObject* parent = nullptr);
    void check(QVector<BookmarkNode*> nodes);
    bool isRunning() const;
    int maxConcurrent() const;
    void setMaxConcurrent(int value);

signals:
    void resultReady(BookmarkNode* node, const HealthResult& result);
    void finished(int total, int failed);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    struct RequestState {
        BookmarkNode* node = nullptr;
        QElapsedTimer timer;
        bool fallbackGet = false;
    };

    QNetworkAccessManager manager_;
    QQueue<BookmarkNode*> pending_;
    QHash<QNetworkReply*, RequestState> states_;
    int total_ = 0;
    int failed_ = 0;
    int maxConcurrent_ = 8;
    bool running_ = false;

    void startMore();
    void startRequest(BookmarkNode* node, bool fallbackGet);
    HealthResult classify(QNetworkReply* reply, const RequestState& state) const;
};
