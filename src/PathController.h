#ifndef APPLICATION_PATHCONTROLLER_H
#define APPLICATION_PATHCONTROLLER_H

#include <QGeoPath>
#include <QGeoPositionInfoSource>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

// This class used to be declared with Verdigris (W_OBJECT/W_PROPERTY/W_SIGNAL)
// rather than moc. The vendored copy of Verdigris was from 2018 and did not
// compile against Qt 6 - it reaches for QMetaObject::QueryPropertyUser and
// QByteArrayDataPtr, both gone - and it existed in this tree for exactly one
// class of three properties. Plain Q_OBJECT does the same job.
class PathController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QGeoPath geopath READ geoPath WRITE setGeoPath NOTIFY geopathChanged)
    Q_PROPERTY(QGeoCoordinate center READ center WRITE setCenter NOTIFY centerChanged)
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY distanceChanged)

  public:
    PathController(QObject *parent = 0);

    QGeoPath geoPath() const { return mGeoPath; }

    void setGeoPath(const QGeoPath &geoPath) {
        if (geoPath == mGeoPath) {
            return;
        }
        mGeoPath = geoPath;
        emit geopathChanged();
    }

    QGeoCoordinate center() const { return mCenter; }

    void setCenter(const QGeoCoordinate &center) {
        if (center == mCenter) {
            return;
        }
        mCenter = center;
        emit centerChanged();
    }

    double distance() const { return mDistance; }

    void setDistance(double distance) {
        if (qFuzzyCompare(distance, mDistance)) {
            return;
        }
        mDistance = distance;
        emit distanceChanged();
    }

  signals:
    void geopathChanged();
    void centerChanged();
    void distanceChanged();

  private:
    QGeoPath mGeoPath;
    QGeoCoordinate mCenter;
    double mDistance = 0.0;
};

#endif // APPLICATION_PATHCONTROLLER_H
