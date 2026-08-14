#ifndef QTCHARTSCOMPAT_H
#define QTCHARTSCOMPAT_H

#include <QtGlobal>
#include <QtCharts/QChartGlobal>

/*
 * Qt 5 puts every Charts class in namespace QtCharts and hands out
 * QT_CHARTS_USE_NAMESPACE to pull it in. Qt 6 dropped the namespace, and the
 * macro along with it - 6.8.2's qchartglobal.h defines neither. Defining it away
 * here lets the sources name QChart, QChartView and friends unqualified on both.
 */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#ifndef QT_CHARTS_USE_NAMESPACE
#define QT_CHARTS_USE_NAMESPACE
#endif
#endif

QT_CHARTS_USE_NAMESPACE

#endif // QTCHARTSCOMPAT_H
