// SPDX-FileCopyrightText: 2017 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "dblureffectwidget.h"
#include "private/dblureffectwidget_p.h"
#include "dplatformwindowhandle.h"

#include <DWindowManagerHelper>
#include <DGuiApplicationHelper>

#include <QPainter>
#include <QBackingStore>
#include <QPaintEvent>
#include <QDebug>

#include <qpa/qplatformbackingstore.h>
#include <private/qwidget_p.h>
#ifndef slots
#define slots Q_SLOTS
#endif

#define private public

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
#include <private/qwidgetrepaintmanager_p.h>
#else
#include <private/qwidgetbackingstore_p.h>
#endif

#undef private

#define MASK_COLOR_ALPHA_DEFAULT 204

QT_BEGIN_NAMESPACE
Q_WIDGETS_EXPORT void qt_blurImage(QPainter *p, QImage &blurImage, qreal radius, bool quality, bool alphaOnly, int transposed = 0);
QT_END_NAMESPACE

DGUI_USE_NAMESPACE

DWIDGET_BEGIN_NAMESPACE

QMultiHash<QWidget *, const DBlurEffectWidget *> DBlurEffectWidgetPrivate::blurEffectWidgetHash;
QHash<const DBlurEffectWidget *, QWidget *> DBlurEffectWidgetPrivate::windowOfBlurEffectHash;

DBlurEffectWidgetPrivate::DBlurEffectWidgetPrivate(DBlurEffectWidget *qq)
    : DObjectPrivate(qq)
{

}

bool DBlurEffectWidgetPrivate::isBehindWindowBlendMode() const
{
    D_QC(DBlurEffectWidget);

    return blendMode == DBlurEffectWidget::BehindWindowBlend
           || q->isTopLevel();
}

bool DBlurEffectWidgetPrivate::isFull() const
{
    D_QC(DBlurEffectWidget);

    return full || (q->isTopLevel() && !(blurRectXRadius && blurRectYRadius) && maskPath.isEmpty());
}

void DBlurEffectWidgetPrivate::addToBlurEffectWidgetHash()
{
    D_QC(DBlurEffectWidget);

    QWidget *oldTopLevelWidget = windowOfBlurEffectHash.value(q);

    if (oldTopLevelWidget) {
        blurEffectWidgetHash.remove(oldTopLevelWidget, q);
        updateWindowBlurArea(oldTopLevelWidget);
    }

    QWidget *topLevelWidget = q->topLevelWidget();

    blurEffectWidgetHash.insertMulti(topLevelWidget, q);
    windowOfBlurEffectHash[q] = topLevelWidget;
    updateWindowBlurArea(topLevelWidget);
}

void DBlurEffectWidgetPrivate::removeFromBlurEffectWidgetHash()
{
    D_QC(DBlurEffectWidget);

    QWidget *topLevelWidget = windowOfBlurEffectHash.value(q);

    if (!topLevelWidget) {
        return;
    }

    blurEffectWidgetHash.remove(topLevelWidget, q);
    windowOfBlurEffectHash.remove(q);
    updateWindowBlurArea(topLevelWidget);
}

bool DBlurEffectWidgetPrivate::updateWindowBlurArea()
{
    D_QC(DBlurEffectWidget);

    QWidget *topLevelWidget = windowOfBlurEffectHash.value(q);

    return topLevelWidget && updateWindowBlurArea(topLevelWidget);
}

void DBlurEffectWidgetPrivate::setMaskAlpha(const quint8 alpha) {
    maskAlpha = alpha;

    // refresh alpha
    setMaskColor(maskColor);
}

quint8 DBlurEffectWidgetPrivate::getMaskColorAlpha() const
{
    if (maskAlpha < 0)
        return isBehindWindowBlendMode() ? 102 : 204;

    return static_cast<quint8>(maskAlpha);
}

QColor DBlurEffectWidgetPrivate::getMaskColor(const QColor &baseColor) const
{
    QColor color = baseColor;
    DGuiApplicationHelper::ColorType ct = DGuiApplicationHelper::toColorType(color);

    if (DGuiApplicationHelper::DarkType == ct) {
        color = DGuiApplicationHelper::adjustColor(color, 0, 0, -10, 0, 0, 0, 0);
    } else {
        color = DGuiApplicationHelper::adjustColor(color, 0, 0, -5, 0, 0, 0, 0);
    }

    int maskAlpha = this->getMaskColorAlpha();

    if (!isBehindWindowBlendMode() || DWindowManagerHelper::instance()->hasComposite()) {
        color.setAlpha(maskAlpha);
    } else {
        return ct == DGuiApplicationHelper::DarkType ? "#202020" : "#D2D2D2";
    }

    return color;
}

void DBlurEffectWidgetPrivate::resetSourceImage()
{
    // ?????????????????????image??????????????????
    // ???????????????????????????
    if (customSourceImage || group)
        return;

    sourceImage = QImage();
}

void DBlurEffectWidgetPrivate::setMaskColor(const QColor &color)
{
    maskColor = color;

    if (isBehindWindowBlendMode()) {
        maskColor.setAlpha(DWindowManagerHelper::instance()->hasComposite() ? getMaskColorAlpha() : MASK_COLOR_ALPHA_DEFAULT);
    }

    D_Q(DBlurEffectWidget);

    q->update();
}

bool DBlurEffectWidgetPrivate::updateWindowBlurArea(QWidget *topLevelWidget)
{
    if (!topLevelWidget->isVisible()) {
        return false;
    }

    QList<const DBlurEffectWidget *> blurEffectWidgetList = blurEffectWidgetHash.values(topLevelWidget);

    bool isExistMaskPath = false;

    Q_FOREACH (const DBlurEffectWidget *w, blurEffectWidgetList) {
        if (!w->d_func()->blurEnabled) {
            continue;
        }

        // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        if (w->d_func()->isFull() && w->isVisible()) {
            DPlatformWindowHandle handle(topLevelWidget);

            if (!handle.enableBlurWindow()) {
                handle.setEnableBlurWindow(true);
            }

            return true;
        }

        if (!w->d_func()->maskPath.isEmpty() && w->isVisible()) {
            isExistMaskPath = true;
            break;
        }
    }

    // ???????????????????????????????????????????????????
    DPlatformWindowHandle handle(topLevelWidget);

    if (handle.enableBlurWindow()) {
        handle.setEnableBlurWindow(false);
    }

    bool ok = false;

    if (isExistMaskPath) {
        QList<QPainterPath> pathList;

        Q_FOREACH (const DBlurEffectWidget *w, blurEffectWidgetList) {
            if (!w->d_func()->blurEnabled) {
                continue;
            }

            if (!w->isVisible()) {
                continue;
            }

            QPainterPath p;
            QRect r = w->rect();

            r.moveTopLeft(w->mapTo(topLevelWidget, r.topLeft()));
            p.addRoundedRect(r, w->blurRectXRadius(), w->blurRectYRadius());

            if (!w->d_func()->maskPath.isEmpty()) {
                p &= w->d_func()->maskPath.translated(r.topLeft());
            }

            pathList << p;
        }

        ok = handle.setWindowBlurAreaByWM(pathList);
    } else {
        QVector<DPlatformWindowHandle::WMBlurArea> areaList;

        areaList.reserve(blurEffectWidgetList.size());

        Q_FOREACH (const DBlurEffectWidget *w, blurEffectWidgetList) {
            if (!w->d_func()->blurEnabled) {
                continue;
            }

            if (!w->isVisible()) {
                continue;
            }

            QRect r = w->rect();

            r.moveTopLeft(w->mapTo(topLevelWidget, r.topLeft()));

            areaList << dMakeWMBlurArea(r.x(), r.y(), r.width(), r.height(), w->blurRectXRadius(), w->blurRectYRadius());
        }

        ok = handle.setWindowBlurAreaByWM(areaList);
    }

    if (blurEffectWidgetList.isEmpty()) {
        blurEffectWidgetHash.remove(topLevelWidget);
    }

    return ok;
}

/*!
  \class Dtk::Widget::DBlurEffectWidget
  \inmodule dtkwidget

  \brief ?????????????????????????????????????????????????????????.
  \brief The DBlurEffectWidget class provides widget that backgrounds are blurred and semitranslucent.
  
  ????????????????????? DBlurEffectWidget::BehindWindowBlend ??????????????? DBlurEffectWidget::InWindowBlend DBlurEffectWidget::InWindowBlend
  ??????????????????????????????????????????????????????????????????????????????????????????????????? deepin-wm ??? kwin???
  ????????????DWindowManagerHelper::hasBlurWindow ?????????????????????????????????????????????????????????
  ????????????????????? DPlatformWindowHandle::setWindowBlurAreaByWM ??????????????????????????????
  ????????????????????????????????????????????????????????? DBlurEffectWidget ??????????????????????????????????????????
  ????????????????????????????????????????????? QVector<WMBlurArea> ?????? QList<QPainterPath>
  ??????????????????????????????????????? DBlurEffectWidget::radius ???????????????????????????
  ?????????????????????????????? DClipEffectWidget ???????????????????????????????????? QPlatformBackingStore::toImage()
  ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
  ????????? QImage ???????????????????????????????????????????????????????????????????????????????????????????????????????????????
  ???????????????????????????????????????OpenGL??????????????????????????????QOpenGLWindow ???????????????????????????
  OpenGL????????????????????????????????????????????? QOpenGLWidget ???

  DBlurEffectWidget is QWidget that has blurry background. With different
  blend mode set, DBlurEffectWidget can do in-window-blend, which means the
  the widget is blended with the widgets between the top level window and the
  widget itself, and behind-window-blend, which means the widget is blended with
  the scene behind the window (with the help of WM).
  
  The effect has optional styles can choose from: DBlurEffectWidget::DarkColor, DBlurEffectWidget::LightColor, and
  DBlurEffectWidget::CustomColor. Usually the first two are sufficient, you can also choose
  CustomColor and set the color you want by setting DBlurEffectWidget::maskColor.
  
  \note DBlurEffectWidget with BehindWindowBlend mode will become opaque if
  WM supports no X11 composite protocol.
 */

/*!
  \property DBlurEffectWidget::radius
  \brief ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
  ?????????????????????????????? radius ????????????????????????????????????
  \note ????????????
  \note ??????????????? DBlurEffectWidget::InWindowBlend ?????????
 */

/*!
  \property DBlurEffectWidget::mode
  \brief ??????????????????????????????????????? \a GaussianBlur
  \note ????????????
  \note ??????????????? DBlurEffectWidget::InWindowBlend ?????????
 */

/*!
  \property DBlurEffectWidget::blendMode
  \brief ???????????????????????????????????????????????????????????????????????????????????????
  \note ????????????
  \note ????????????????????????????????? DBlurEffectWidget::InWindowBlend ????????????
 */

/*!
  \property DBlurEffectWidget::blurRectXRadius
  \brief ???????????????x??????????????????????????????????????????0
  \note ????????????
  \sa DBlurEffectWidget::blurRectYRadius DBlurEffectWidget::setMaskPath QPainterPath::addRoundedRect
 */

/*!
  \property DBlurEffectWidget::blurRectYRadius
  \brief ???????????????y??????????????????????????????????????????0
  \note ????????????
  \sa DBlurEffectWidget::blurRectXRadius DBlurEffectWidget::setMaskPath QPainterPath::addRoundedRect
 */

/*!
  \property DBlurEffectWidget::maskColor
  \brief ??????????????????????????????????????????????????????alpha??????????????? \a maskAlpha ????????????
  ???????????????????????????????????????????????? CustomColor?????????????????????????????????????????????????????????
  ???????????????????????????????????????????????????
  \note ????????????
  \sa DBlurEffectWidget::blurRectXRadius DBlurEffectWidget::setMaskColor()
 */

/*!
  \property DBlurEffectWidget::maskAlpha
  \brief maskColor ???alpha????????????????????????????????????????????????????????????????????????????????????102????????????204
  \note ????????????
  \sa DBlurEffectWidget::maskColor
 */

/*!
  \property DBlurEffectWidget::full
  \brief ????????????true?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
  ?????????????????????????????????????????????????????????????????????????????????????????????????????? blurRectXRadius ??? blurRectYRadius
  ??????????????????????????? full ??????????????????????????????????????? true ???????????????????????????
  \note ????????????
  \note ???????????????????????????????????????
  \note ?????????????????????????????? BehindWindowBlend ??? DBlurEffectWidget::InWindowBlend ?????????
  \sa DBlurEffectWidget::blurRectXRadius DBlurEffectWidget::blurRectYRadius
  \sa DBlurEffectWidget::maskColor
  \sa DBlurEffectWidget::blendMode
 */

/*!
  \property DBlurEffectWidget::blurEnabled
  \brief ???????????? true ???????????????????????????????????????????????????
  \note ????????????
 */

/*!
  \fn void DBlurEffectWidget::radiusChanged(int radius)
  \brief ???????????? \a radius ??????????????????????????????
 */
/*!
  \fn void DBlurEffectWidget::modeChanged(BlurMode mode)
  \brief ???????????? \a mode ??????????????????????????????.
 */
/*!
  \fn void DBlurEffectWidget::blendModeChanged(BlendMode blendMode)
  \brief ???????????? \a blendMode ??????????????????????????????
 */
/*!
  \fn void DBlurEffectWidget::blurRectXRadiusChanged(int blurRectXRadius)
  \brief ???????????? \a blurRectXRadius ??????????????????????????????
 */
/*!
  \fn void DBlurEffectWidget::blurRectYRadiusChanged(int blurRectYRadius)
  \brief ???????????? \a blurRectYRadius ??????????????????????????????
 */
/*!
  \fn void DBlurEffectWidget::maskAlphaChanged(quint8 alpha)
  \brief ???????????? \a alpha ??????????????????????????????
  \sa DBlurEffectWidget::maskAlpha
 */
/*!
  \fn void DBlurEffectWidget::maskColorChanged(QColor maskColor)
  \brief ???????????? \a maskColor ??????????????????????????????
 */

/*!
  \enum DBlurEffectWidget::BlurMode
  DBlurEffectWidget::BlurMode ????????????
  
  \value GaussianBlur
  \l {https://zh.wikipedia.org/wiki/????????????}{??????????????????}
 */

/*!
  \enum DBlurEffectWidget::BlendMode
  DBlurEffectWidget::BlendMode ????????????
  \image html blur-effect.png
  
  \var DBlurEffectWidget::BlendMode DBlurEffectWidget::InWindowBlend
  ???????????????????????????????????????
  
  \var DBlurEffectWidget::BlendMode DBlurEffectWidget::BehindWindowBlend
  ??????????????????????????????????????????
  
  \var DBlurEffectWidget::BlendMode DBlurEffectWidget::InWidgetBlend
  ??? DBlurEffectWidget::InWindowBlend??????????????????????????????????????????????????????
  ??????????????? DBlurEffectWidget::updateBlurSourceImage ??????????????????????????????????????????
  ????????????????????????
 */

/*!
  \enum Dtk::Widget::DBlurEffectWidget::MaskColorType
  DBlurEffectWidget::MaskColorType ???????????????????????????????????????????????????
  \a A???????????? DBlurEffectWidget::InWindowBlend ???????????????????????????????????????????????????????????????
  \a B???????????? DBlurEffectWidget::BehindWindowBlend ??? DBlurEffectWidget::InWindowBlend ??????????????????????????????
  \a C???????????? DBlurEffectWidget::BehindWindowBlend ??? DBlurEffectWidget::InWindowBlend ??????????????????????????????
  \sa DBlurEffectWidget::maskAlpha
  
  \value DarkColor
  ??????????????????????????????????????????
  
  \a A???color{black,#000000}???alpha????????????????????????
  \a B???color{#373F47,#373F47}
  \a C???color{rgba(0\,0\,0\,0.8),#CC000000}
  
  \value LightColor
  ??????????????????????????????????????????
  \a A???color{#FFFFFF,#FFFFFF}???alpha????????????????????????
  \a B???color{#FCFCFC,#FCFCFC}
  \a C???color{rgba(255\,255\,255\,0.8),#CCFFFFFF}
  
  \value AutoColor
  ?????????????????????????????????????????????????????????????????????alpha????????????????????????
  
  \value CustomColor
  ???????????????????????? DBlurEffectWidget::setMaskColor ???????????????
 */

/*!
  \brief DBlurEffectWidget::DBlurEffectWidget constructs an instance of DBlurEffectWidget.
  \brief ?????????????????????????????????????????????????????????????????????????????????????????? Qt::WA_TranslucentBackground???
  ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????

  \a parent is passed to QWidget constructor.
  \a parent ???????????????????????????????????? DBlurEffectWidget::BehindWindowBlend ????????????????????? DBlurEffectWidget::InWindowBlend ??????
 */
DBlurEffectWidget::DBlurEffectWidget(QWidget *parent)
    : QWidget(parent)
    , DObject(*new DBlurEffectWidgetPrivate(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setBackgroundRole(QPalette::Window);

    if (!parent) {
        D_D(DBlurEffectWidget);

        d->addToBlurEffectWidgetHash();
    }

    QObject::connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::windowManagerChanged, this, [this] {
        D_D(DBlurEffectWidget);

        d->updateWindowBlurArea();
    });
    QObject::connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasBlurWindowChanged, this, [this] {
        D_D(DBlurEffectWidget);

        d->setMaskColor(d->maskColor);
    });
    QObject::connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasCompositeChanged, this, [this] {
        D_D(const DBlurEffectWidget);

        if (d->maskColorType != CustomColor)
            update();
    });
}

DBlurEffectWidget::~DBlurEffectWidget()
{
    D_D(DBlurEffectWidget);

    if (d->isBehindWindowBlendMode()) {
        d->removeFromBlurEffectWidgetHash();
    }

    // clean group
    if (d->group) {
        d->group->removeWidget(this);
    }
}

/*!
  \brief This property holds the radius of the blur effect.
  
  \note This property has no effect with the DBlurEffectWidget::blendMode set
  to DBlurEffectWidget::BehindWindowBlend.
 */
int DBlurEffectWidget::radius() const
{
    D_DC(DBlurEffectWidget);

    return d->radius;
}

/*!
  \brief This property holds which blur algorithm to be used.
  
  Currently it only supports DBlurEffectWidget::GaussianBlur.
 */
DBlurEffectWidget::BlurMode DBlurEffectWidget::mode() const
{
    D_DC(DBlurEffectWidget);

    return d->mode;
}

/*!
  \brief This property holds which mode is used to blend the widget and its background scene.
 */
DBlurEffectWidget::BlendMode DBlurEffectWidget::blendMode() const
{
    D_DC(DBlurEffectWidget);

    return d->blendMode;
}

/*!
  \brief This property holds the xRadius of the effective background.
  
  The xRadius and yRadius specify the radius of the ellipses defining
  the corners of the effective background.
  
  \sa DBlurEffectWidget::blurRectYRadius
 */
int DBlurEffectWidget::blurRectXRadius() const
{
    D_DC(DBlurEffectWidget);

    return d->blurRectXRadius;
}

/*!
  \brief This property holds the yRadius of the effective background.
  
  The xRadius and yRadius specify the radius of the ellipses defining
  the corners of the effective background.
  
  \sa DBlurEffectWidget::blurRectXRadius
 */
int DBlurEffectWidget::blurRectYRadius() const
{
    D_DC(DBlurEffectWidget);

    return d->blurRectYRadius;
}

/*!
  \brief This property holds the background color of this widget.
  
  It returns predefined colors if the DBlurEffectWidget::maskColorType is set
  to DBlurEffectWidget::DarkColor or BlurEffectWidget::LightColor, returns
  the color set by DBlurEffectWidget::setMaskColor if
  DBlurEffectWidget::maskColorType is set to BlurEffectWidget::CustomColor.
 */
QColor DBlurEffectWidget::maskColor() const
{
    D_DC(DBlurEffectWidget);

    switch ((int)d->maskColorType) {
    case DarkColor:
        return d->getMaskColor(DGuiApplicationHelper::standardPalette(DGuiApplicationHelper::DarkType).window().color());
    case LightColor:
        return d->getMaskColor(DGuiApplicationHelper::standardPalette(DGuiApplicationHelper::LightType).window().color());
    case AutoColor: {
        QColor color = palette().color(backgroundRole());

        return d->getMaskColor(color);
    }
    }

    return d->maskColor;
}

quint8 DBlurEffectWidget::maskAlpha() const {
    D_DC(DBlurEffectWidget);

    return d->getMaskColorAlpha();
}

/*!
  \brief DBlurEffectWidget::setMaskPath set custom area as the effective background.
  \brief ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????

  \a path a QPainterPath to be used as the effectvie background.
  \warning ??????????????????????????????????????????????????????????????????????????????????????????
  \sa DBlurEffectWidget::blurRectXRadius DBlurEffectWidget::blurRectYRadius
 */
void DBlurEffectWidget::setMaskPath(const QPainterPath &path)
{
    D_D(DBlurEffectWidget);

    if (d->maskPath == path) {
        return;
    }

    d->maskPath = path;

    update();
}

/*!
  \brief DBlurEffectWidget::setSourceImage
  \a image
  \a autoScale
  \warning
 */
void DBlurEffectWidget::setSourceImage(const QImage &image, bool autoScale)
{
    D_D(DBlurEffectWidget);

    d->sourceImage = image;
    d->customSourceImage = !image.isNull();
    d->autoScaleSourceImage = autoScale && d->customSourceImage;

    if (autoScale && isVisible()) {
        d->sourceImage.setDevicePixelRatio(devicePixelRatioF());
        d->sourceImage = d->sourceImage.scaled((size() + QSize(d->radius * 1, d->radius * 2)) * devicePixelRatioF());
        d->sourceImage.setDevicePixelRatio(devicePixelRatioF());
    }
}

/*!
  \brief DBlurEffectWidget::isFull
  \return???true ?????????????????????????????????,?????????false
 */
bool DBlurEffectWidget::isFull() const
{
    D_DC(DBlurEffectWidget);

    return d->full;
}

/*!
  \brief DBlurEffectWidget::blurEnabled
  \return true ???????????????????????????
 */
bool DBlurEffectWidget::blurEnabled() const
{
    D_DC(DBlurEffectWidget);

    return d->blurEnabled;
}

/*!
  \brief DBlurEffectWidget::setRadius
  \a radius?????????????????????????????????????????????????????????????????????????????????radiusChanged
 */
void DBlurEffectWidget::setRadius(int radius)
{
    D_D(DBlurEffectWidget);

    if (d->radius == radius) {
        return;
    }

    d->radius = radius;
    d->resetSourceImage();

    update();

    Q_EMIT radiusChanged(radius);
}

/*!
  \brief DBlurEffectWidget::setMode
  \a mode ??????????????????,???????????????????????????GaussianBlur
 */
void DBlurEffectWidget::setMode(DBlurEffectWidget::BlurMode mode)
{
    D_D(DBlurEffectWidget);

    if (d->mode == mode) {
        return;
    }

    d->mode = mode;

    Q_EMIT modeChanged(mode);
}

/*!
  \brief DBlurEffectWidget::setBlendMode
  \a blendMode ?????????????????????????????????????????????blendModeChanged??????
 */
void DBlurEffectWidget::setBlendMode(DBlurEffectWidget::BlendMode blendMode)
{
    D_D(DBlurEffectWidget);

    if (d->blendMode == blendMode) {
        return;
    }

    if (blendMode == BehindWindowBlend) {
        d->addToBlurEffectWidgetHash();

        // ??????????????????????????????????????????
        topLevelWidget()->removeEventFilter(this);
    } else {
        if (blendMode != BehindWindowBlend) {
            d->maskColor.setAlpha(d->getMaskColorAlpha());
        }

        if (d->blendMode == BehindWindowBlend) {
            d->removeFromBlurEffectWidgetHash();
        }

        if (isVisible()) {
            // ????????????????????????????????????
            topLevelWidget()->installEventFilter(this);
        }
    }

    // ?????????????????? d->blendMode ?????????????????????
    d->blendMode = blendMode;
    update();

    Q_EMIT blendModeChanged(blendMode);
}

/*!
  \brief DBlurEffectWidget::setBlurRectXRadius
  \a blurRectXRadius ?????????????????????X????????????
 */
void DBlurEffectWidget::setBlurRectXRadius(int blurRectXRadius)
{
    D_D(DBlurEffectWidget);

    if (d->blurRectXRadius == blurRectXRadius) {
        return;
    }

    d->blurRectXRadius = blurRectXRadius;

    update();

    Q_EMIT blurRectXRadiusChanged(blurRectXRadius);
}

/*!
  \brief DBlurEffectWidget::setBlurRectYRadius
  \a blurRectYRadius ?????????????????????Y????????????
 */
void DBlurEffectWidget::setBlurRectYRadius(int blurRectYRadius)
{
    D_D(DBlurEffectWidget);

    if (d->blurRectYRadius == blurRectYRadius) {
        return;
    }

    d->blurRectYRadius = blurRectYRadius;

    update();

    Q_EMIT blurRectYRadiusChanged(blurRectYRadius);
}

/*!
  \brief DBlurEffectWidget::setMaskAlpha
  \a alpha?????????Alpha??????,???????????????maskAlphaChanged??????
 */
void DBlurEffectWidget::setMaskAlpha(quint8 alpha) {
    D_D(DBlurEffectWidget);

    if (alpha == d->maskAlpha) return;

    d->setMaskAlpha(alpha);

    Q_EMIT maskAlphaChanged(alpha);
}

/*!
  \brief DBlurEffectWidget::setMaskColor
  \a maskColor ??????mask?????????
 */
void DBlurEffectWidget::setMaskColor(QColor maskColor)
{
    D_D(DBlurEffectWidget);

    if (!maskColor.isValid()) {
        maskColor = Qt::transparent;
    }

    if (d->maskColor == maskColor) {
        return;
    }

    d->maskColorType = CustomColor;
    d->setMaskColor(maskColor);

    Q_EMIT maskColorChanged(maskColor);
}

/*!
  \brief ?????????????????????????????????????????? MaskColorType::CustomColor
  \a type
 */
void DBlurEffectWidget::setMaskColor(DBlurEffectWidget::MaskColorType type)
{
    D_D(DBlurEffectWidget);

    if (d->maskColorType == type) {
        return;
    }

    d->maskColorType = type;

    update();
}

/*!
  \brief DBlurEffectWidget::setFull
  \a full ????????????????????????????????????????????????
 */
void DBlurEffectWidget::setFull(bool full)
{
    D_D(DBlurEffectWidget);

    if (d->full == full)
        return;

    d->full = full;
    d->updateWindowBlurArea();

    Q_EMIT fullChanged(full);
}

/*!
  \brief DBlurEffectWidget::setBlurEnabled
  \a blurEnabled ????????????????????????????????????
 */
void DBlurEffectWidget::setBlurEnabled(bool blurEnabled)
{
    D_D(DBlurEffectWidget);

    if (d->blurEnabled == blurEnabled)
        return;

    d->blurEnabled = blurEnabled;
    d->updateWindowBlurArea();
    update();

    Q_EMIT blurEnabledChanged(d->blurEnabled);
}

inline QRect operator *(const QRect &rect, qreal scale)
{
    return QRect(rect.left() * scale, rect.top() * scale, rect.width() * scale, rect.height() * scale);
}

/*!
  \brief DBlurEffectWidget::updateBlurSourceImage
  \a ren ?????????????????????????????????
 */
void DBlurEffectWidget::updateBlurSourceImage(const QRegion &ren)
{
    D_D(DBlurEffectWidget);

    // ???????????????????????????????????????????????????????????????
    if (d->customSourceImage || d->group)
        return;

    const qreal device_pixel_ratio = devicePixelRatioF();
    const QPoint point_offset = mapTo(window(), QPoint(0, 0));

    if (d->sourceImage.isNull()) {
        const QRect &tmp_rect = rect().translated(point_offset).adjusted(-d->radius, -d->radius, d->radius, d->radius);

        d->sourceImage = window()->backingStore()->handle()->toImage().copy(tmp_rect * device_pixel_ratio);
        d->sourceImage = d->sourceImage.scaledToWidth(d->sourceImage.width() / device_pixel_ratio);
    } else {
        QPainter pa_image(&d->sourceImage);

        pa_image.setCompositionMode(QPainter::CompositionMode_Source);

        if (device_pixel_ratio > 1) {
            const QRect &tmp_rect = this->rect().translated(point_offset);
            QImage area = window()->backingStore()->handle()->toImage().copy(tmp_rect * device_pixel_ratio);
            area = area.scaledToWidth(area.width() / device_pixel_ratio);

            for (const QRect &rect : ren.rects()) {
                pa_image.drawImage(rect.topLeft() + QPoint(d->radius, d->radius), rect == area.rect() ? area : area.copy(rect));
            }
        } else {
            for (const QRect &rect : ren.rects()) {
                pa_image.drawImage(rect.topLeft() + QPoint(d->radius, d->radius),
                                   window()->backingStore()->handle()->toImage().copy(rect.translated(point_offset)));
            }
        }

        pa_image.end();
    }
}

DBlurEffectWidget::DBlurEffectWidget(DBlurEffectWidgetPrivate &dd, QWidget *parent)
    : QWidget(parent)
    , DObject(dd)
{

}

void DBlurEffectWidget::paintEvent(QPaintEvent *event)
{
    D_D(DBlurEffectWidget);

    if (!d->blurEnabled)
        return;

    QPainter pa(this);

    if (d->blurRectXRadius > 0 || d->blurRectYRadius > 0) {
        QPainterPath path;

        path.addRoundedRect(rect(), d->blurRectXRadius, d->blurRectYRadius);
        pa.setRenderHint(QPainter::Antialiasing);
        pa.setClipPath(path);
    }

    if (!d->maskPath.isEmpty()) {
        QPainterPath path = pa.clipPath();

        if (path.isEmpty()) {
            path = d->maskPath;
        } else {
            path &= d->maskPath;
        }

        pa.setClipPath(path);
    }

    if (d->isBehindWindowBlendMode()) {
        pa.setCompositionMode(QPainter::CompositionMode_Source);
    } else {
        // ???????????????????????????sourceImage?????????
        if (d->blendMode != InWidgetBlend) {
            updateBlurSourceImage(event->region());
        }

        if (d->customSourceImage || !d->sourceImage.isNull()) {
            int radius = d->radius;
            qreal device_pixel_ratio = devicePixelRatioF();
            QImage image;
            const QRect &paintRect = event->rect();

            if (d->customSourceImage) {
                image = d->sourceImage.copy(paintRect.adjusted(0, 0, 2 * radius, 2 * radius) * device_pixel_ratio);
                image.setDevicePixelRatio(device_pixel_ratio);
                pa.setOpacity(0.2);
            } else {// ???customSourceImage??????????????????????????????
                image = d->sourceImage.copy(paintRect.adjusted(0, 0, 2 * radius, 2 * radius));
            }

            QTransform old_transform = pa.transform();
            pa.translate(paintRect.topLeft() - QPoint(radius, radius));
            qt_blurImage(&pa, image, radius, false, false);
            pa.setTransform(old_transform);
            pa.setOpacity(1);
        } else if (d->group) { // ?????????
            d->group->paint(&pa, this);
        }
    }

    pa.fillRect(rect(), maskColor());
}

void DBlurEffectWidget::moveEvent(QMoveEvent *event)
{
    D_D(DBlurEffectWidget);

    if (isTopLevel()) {
        return QWidget::moveEvent(event);
    }

    if (d->blendMode == DBlurEffectWidget::InWindowBlend
            || d->blendMode == DBlurEffectWidget::BehindWindowBlend) {
        d->resetSourceImage();

        return QWidget::moveEvent(event);
    }

    d->updateWindowBlurArea();

    QWidget::moveEvent(event);
}

void DBlurEffectWidget::resizeEvent(QResizeEvent *event)
{
    D_D(DBlurEffectWidget);

    d->resetSourceImage();

    if (!d->isBehindWindowBlendMode()) {
        if (d->autoScaleSourceImage) {
            d->sourceImage = d->sourceImage.scaled((size() + QSize(d->radius * 1, d->radius * 2)) * devicePixelRatioF());
            d->sourceImage.setDevicePixelRatio(devicePixelRatioF());
        }

        return QWidget::resizeEvent(event);
    }

    d->updateWindowBlurArea();

    QWidget::resizeEvent(event);
}

void DBlurEffectWidget::showEvent(QShowEvent *event)
{
    D_D(DBlurEffectWidget);

    if (!d->isBehindWindowBlendMode()) {
        if (d->autoScaleSourceImage) {
            d->sourceImage = d->sourceImage.scaled((size() + QSize(d->radius * 1, d->radius * 2)) * devicePixelRatioF());
            d->sourceImage.setDevicePixelRatio(devicePixelRatioF());
        }

        // ????????????????????????????????????
        topLevelWidget()->installEventFilter(this);

        return QWidget::showEvent(event);
    }

    d->addToBlurEffectWidgetHash();

    QWidget::showEvent(event);
}

void DBlurEffectWidget::hideEvent(QHideEvent *event)
{
    D_D(DBlurEffectWidget);

    if (!d->isBehindWindowBlendMode()) {
        // ?????????????????????
        topLevelWidget()->removeEventFilter(this);

        return QWidget::hideEvent(event);
    }

    d->removeFromBlurEffectWidgetHash();

    QWidget::hideEvent(event);
}

void DBlurEffectWidget::changeEvent(QEvent *event)
{
    D_D(DBlurEffectWidget);

    if (!d->isBehindWindowBlendMode()) {
        return QWidget::changeEvent(event);
    }

    if (event->type() == QEvent::ParentAboutToChange) {
        d->removeFromBlurEffectWidgetHash();
    } else if (event->type() == QEvent::ParentChange) {
        d->addToBlurEffectWidgetHash();
    }

    QWidget::changeEvent(event);
}

bool DBlurEffectWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::UpdateRequest) {
        return QWidget::eventFilter(watched, event);
    }

    // ?????????????????????????????????????????????????????????????????????????????????????????????
    // ???????????????????????????????????????????????????DBlurEffectWidget??????????????????????????????????????????????????????????????????????????????????????????????????????
    if (QWidget *widget = qobject_cast<QWidget*>(watched)) {
        auto wd = QWidgetPrivate::get(widget);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        if (!wd->maybeRepaintManager()) {
#else
        if (!wd->maybeBackingStore()) {
#endif
            return QWidget::eventFilter(watched, event);
        }

        // ????????????????????????
        QRegion dirty;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        for (const QWidget *w : wd->maybeRepaintManager()->dirtyWidgets) {
#else
        for (const QWidget *w : wd->maybeBackingStore()->dirtyWidgets) {
#endif
            dirty |= QWidgetPrivate::get(w)->dirty.translated(w->mapToGlobal(QPoint(0, 0)));
        }

        if (dirty.isEmpty()) {
            return QWidget::eventFilter(watched, event);
        }

        D_D(DBlurEffectWidget);
        const QPoint &offset = mapToGlobal(QPoint(0, 0));
        const QRect frame_rect = rect() + QMargins(d->radius, d->radius, d->radius, d->radius);
        QRegion radius_edge = QRegion(frame_rect) - QRegion(rect());

        // ???????????????????????????????????????????????????????????????radius????????????????????????????????????????????????
        if (!(dirty & radius_edge.translated(offset)).isEmpty()) {
            // ????????????????????????????????????source image
            d->resetSourceImage();

            if (d->blendMode == InWidgetBlend)
                Q_EMIT blurSourceImageDirtied();
            else
                update();
        }
    }

    return QWidget::eventFilter(watched, event);
}

class DBlurEffectGroupPrivate : public DTK_CORE_NAMESPACE::DObjectPrivate
{
public:
    DBlurEffectGroupPrivate(DBlurEffectGroup *qq)
        : DObjectPrivate(qq)
    {

    }

    D_DECLARE_PUBLIC(DBlurEffectGroup)
    QHash<DBlurEffectWidget*, QPoint> effectWidgetMap;
    QPixmap blurPixmap;
};

DBlurEffectGroup::DBlurEffectGroup()
    : DObject(*new DBlurEffectGroupPrivate(this))
{

}

DBlurEffectGroup::~DBlurEffectGroup()
{
    D_DC(DBlurEffectGroup);

    for (DBlurEffectWidget *widget : d->effectWidgetMap.keys()) {
        widget->d_func()->group = nullptr;
        widget->update();
    }
}

void DBlurEffectGroup::setSourceImage(QImage image, int blurRadius)
{
    D_D(DBlurEffectGroup);

    if (image.isNull()) {
        d->blurPixmap = QPixmap();
        return;
    }


    if (blurRadius > 0) {
        QImage tmp(image.size(), image.format());
        QPainter pa(&tmp);
        qt_blurImage(&pa, image, blurRadius, false, false);
        pa.end();
        d->blurPixmap = QPixmap::fromImage(tmp);
    } else {
        d->blurPixmap = QPixmap::fromImage(image);
    }

    d->blurPixmap.setDevicePixelRatio(image.devicePixelRatio());

    // ?????????????????????
    for (auto begin = d->effectWidgetMap.constBegin(); begin != d->effectWidgetMap.constEnd(); ++begin) {
        begin.key()->update();
    }
}

void DBlurEffectGroup::addWidget(DBlurEffectWidget *widget, const QPoint &offset)
{
    if (widget->d_func()->group && widget->d_func()->group != this) {
        widget->d_func()->group->removeWidget(widget);
    }

    widget->d_func()->group = this;
    D_D(DBlurEffectGroup);
    d->effectWidgetMap[widget] = offset;

    widget->update();
}

void DBlurEffectGroup::removeWidget(DBlurEffectWidget *widget)
{
    D_D(DBlurEffectGroup);

    if (d->effectWidgetMap.remove(widget)) {
        widget->d_func()->group = nullptr;
        widget->update();
    }
}

void DBlurEffectGroup::paint(QPainter *pa, DBlurEffectWidget *widget) const
{
    D_DC(DBlurEffectGroup);

    pa->drawPixmap(widget->rect(), d->blurPixmap, widget->geometry().translated(d->effectWidgetMap[widget]));
}

DWIDGET_END_NAMESPACE
