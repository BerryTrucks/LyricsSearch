#include "LyricWikia.hpp"
#include <QUrl>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/ListView>
#include <bb/cascades/Container>
#include <bb/data/JsonDataAccess>
#include <bb/system/SystemToast>
#include <bb/system/SystemUiPosition>
#include <bb/PackageInfo>

using namespace bb::cascades;
using namespace bb::data;
using namespace bb::system;

LyricWikia::LyricWikia() : QObject() {
    _netConfigMan = new QNetworkConfigurationManager(this);
    _netAccessMan = new QNetworkAccessManager(this);
    _favourites = new QMapListDataModel();
    _searchResults = new QMapListDataModel();

    //Try loading favourites from local file
    JsonDataAccess jda;
    QString path = QDir::currentPath() + "/app/native/assets/resources/favourites.json";
    QVariantList list = jda.load(path).value<QVariantList>();
    if (jda.hasError()) {
        qWarning() << Q_FUNC_INFO << "Could not read favourites" << jda.error();
        return;
    }
    foreach (QVariant v, list) {
        QVariantMap data = v.toMap();
        _favourites->append(data);
        qDebug() << Q_FUNC_INFO << "New favourite added:" << data;
    }

    QmlDocument *qml = QmlDocument::create("asset:///Search.qml").parent(this);
    qml->setContextProperty("app", this);
    _root = qml->createRootObject<NavigationPane>();
    Application::instance()->setScene(_root);

    QDeclarativeEngine *engine = QmlDocument::defaultDeclarativeEngine();
    QDeclarativeContext *rootContext = engine->rootContext();
    rootContext->setContextProperty("app", this);
    rootContext->setContextProperty("favouritesDataModel", _favourites);
    rootContext->setContextProperty("searchResultsDataModel", _searchResults);

    bool ok;
    ok = connect(_netAccessMan, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(onFinished(QNetworkReply*)));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}

LyricWikia::~LyricWikia() {
    JsonDataAccess jda;
    QString path = QDir::currentPath() + "/app/native/assets/resources/favourites.json";
    QVariantList list;
    for (int i = 0; i < _favourites->size(); ++i) {
        list.append(_favourites->value(i));
    }
    jda.save(list, path);
}

QString LyricWikia::getVersionNumber() {
    bb::PackageInfo pi;
    return pi.version();
}

void LyricWikia::addFavourite(QVariantMap fav) {
    qDebug() << Q_FUNC_INFO << "Adding favourite:" << fav;
    _favourites->append(fav);
    toast("Favourite Added");
}

void LyricWikia::removeFavourite(QVariantMap fav) {
    qDebug() << Q_FUNC_INFO << "Remove favourite:" << fav;

    for (int i = 0; i < _favourites->size(); ++i) {
        QVariantMap m = _favourites->value(i);
        if (fav.value("artist").toString() == m.value("artist").toString() &&
                fav.value("song").toString() == m.value("song").toString()) {
            _favourites->removeAt(i);
            qDebug() << Q_FUNC_INFO << "Favourite removed.";
            toast("Favourite Removed");
            return;
        }
    }
}

void LyricWikia::toast(QString message) {
    SystemToast *toast = new SystemToast(this);
    toast->setBody(message);
    toast->setPosition(SystemUiPosition::BottomCenter);
    toast->show();
}

void LyricWikia::search(QVariantMap query) {
    Page *page = _root->top();
    Container *container = page->findChild<Container*>("activityContainer");
    if (container) {
        container->setVisible(true);
    } else {
        qDebug() << Q_FUNC_INFO << "No activity container on top page";
    }

    QString artist = query.value("artist").toString();
    QString song = query.value("song").toString();

    QUrl url;
    url.setUrl(QString("http://lyrics.wikia.com/api.php"));

    url.addQueryItem("fmt", "realjson");
    url.addQueryItem("artist", artist);
    if (!song.isEmpty()) {
        url.addQueryItem("song", song);
    }

    QNetworkRequest req(url);
    if (!song.isEmpty()) {
        req.setAttribute(QNetworkRequest::User, QVariant(Song));
    } else {
        req.setAttribute(QNetworkRequest::User, QVariant(Artist));
    }

    qDebug() << Q_FUNC_INFO << url;
    _netAccessMan->get(req);
}

void LyricWikia::onFinished(QNetworkReply *reply) {
    QString response = reply->readAll();
    qDebug() << Q_FUNC_INFO << response;

    _searchResults->clear();

    if (reply->error() == QNetworkReply::NoError) {
        JsonDataAccess jda;
        QVariantMap map = jda.loadFromBuffer(response).value<QVariantMap>();
        if (jda.hasError()) {
            qWarning() << Q_FUNC_INFO << "Couldn't read response into JSON:" << jda.error();
            qWarning() << Q_FUNC_INFO << response;
            return;
        }
        //User searched for just artist
        if (reply->request().attribute(QNetworkRequest::User) == Artist) {
            qDebug() << Q_FUNC_INFO << "Album search results:";
        }
        //User searched for songs
        else if (reply->request().attribute(QNetworkRequest::User) == Song) {
            if (map.value("lyrics").toString() == "Not found") {
                map.insert("url", "");
            } else {
                //Fix special characters in URL
                QString url = QUrl::fromPercentEncoding(map.value("url").toUrl().toString().toUtf8());
                map.insert("url", url);
                //Song and artist come back wacky with special characters, so take them from URL
                QString info = url.split("http://lyrics.wikia.com/", QString::SkipEmptyParts)[0];
                map.insert("artist", info.split(":")[0].replace("_", " "));
                map.insert("song", info.split(":")[1].replace("_", " "));
                qDebug() << Q_FUNC_INFO << "Song search results:";
            }
        }
        qDebug() << Q_FUNC_INFO << map;
        _searchResults->append(map);
    } else {
        qWarning() << Q_FUNC_INFO << "Reply from" << reply->url() << "contains error" << reply->errorString();
        qWarning() << Q_FUNC_INFO << response;
    }

    reply->deleteLater();

    Page *page = _root->top();
    Container *container = page->findChild<Container*>("activityContainer");
    if (container) {
        container->setVisible(false);
    } else {
        qDebug() << Q_FUNC_INFO << "No activity container on top page";
    }
}
