// SPDX-FileCopyrightText: 2017 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "dstyleditemdelegate.h"
#include "dobject_p.h"
#include "dstyleoption.h"
#include "dpalettehelper.h"
#include "dstyle.h"

#include "dlistview.h"

#include <QDebug>
#include <QApplication>
#include <QTextLayout>
#include <DStyle>
#include <QPainter>
#include <QListView>
#include <QLineEdit>
#include <QTableView>
#include <QListWidget>
#include <QPointer>
#include <private/qlayoutengine_p.h>
#include <DGuiApplicationHelper>
#include <DDciIcon>

Q_DECLARE_METATYPE(QMargins)

DWIDGET_BEGIN_NAMESPACE

struct ActionListData : public QSharedData {
    explicit ActionListData() { }
    explicit ActionListData(const DViewItemActionList& v)
        : list(v)
    {
    }
    ~ActionListData ()
    {
        qDeleteAll(list);
    }
    DViewItemActionList list;
};

class ActionList
{
public:
    explicit ActionList() { }
    explicit ActionList(ActionListData *data)
        : m_data(data)
    {
    }
    inline bool isValid() const
    {
        return m_data;
    }
    inline const ActionListData* constData() const
    {
        return m_data.constData();
    }
private:
    friend QDataStream &operator<<(QDataStream &s, const ActionList &v);
    friend QDataStream &operator>>(QDataStream &s, ActionList &v);
    QSharedDataPointer<ActionListData> m_data;
};

QDataStream &operator<<(QDataStream &ds, const ActionList &v)
{
    quintptr data = reinterpret_cast<quintptr>(v.m_data.data());
    ds << data;
    return ds;
}

QDataStream &operator>>(QDataStream &ds, ActionList &v)
{
    quintptr data;
    ds >> data;
    v.m_data = reinterpret_cast<ActionListData *>(data);
    return ds;
}
DWIDGET_END_NAMESPACE

Q_DECLARE_METATYPE(DTK_WIDGET_NAMESPACE::ActionList)

DWIDGET_BEGIN_NAMESPACE
static void saveDViewItemActionList(QDataStream &s, const void *d)
{
    const ActionList &data = *static_cast<const ActionList*>(d);
    s << data;
}

static void loadDViewItemActionList(QDataStream &s, void *d)
{
    ActionList &data = *static_cast<ActionList*>(d);
    s >> data;
}

__attribute__((constructor))
static void registerMetaType ()
{
    // register DViewItemActionList's stream operator to support that QMetaType can using save and load function.
    QMetaType::registerStreamOperators(QMetaTypeId<DTK_WIDGET_NAMESPACE::ActionList>::qt_metatype_id(),
                                       saveDViewItemActionList,
                                       loadDViewItemActionList);
}

static DViewItemActionList qvariantToActionList(const QVariant &v)
{
    const ActionList &wrapper = v.value<ActionList>();
    if (wrapper.isValid())
        return wrapper.constData()->list;

    return DViewItemActionList();
}

static QVariant actionListToQVariant(const DViewItemActionList &v)
{
    ActionList wrapper(new ActionListData(v));
    return QVariant::fromValue(wrapper);
}

class DViewItemActionPrivate : public DCORE_NAMESPACE::DObjectPrivate
{
public:
    DViewItemActionPrivate(DViewItemAction *qq)
        : DObjectPrivate(qq)
    {

    }

    Qt::Alignment alignment;
    QSize iconSize;
    QSize maxSize;
    QMargins clickMargins;
    bool clickable = false;
    DDciIcon dciIcon;
    QPointer<QWidget> widget = nullptr;

    qint8 colorType = -1;
    qint8 colorRole = -1;
    qint8 fontSize = -1;
};

class DStyledItemDelegatePrivate : public DCORE_NAMESPACE::DObjectPrivate
{
public:
    DStyledItemDelegatePrivate(DStyledItemDelegate *qq)
        : DObjectPrivate(qq)
    {

    }

    static QSize actionSize(const DViewItemAction *action, const QSize &max, const QSize &fallbackIconSize, int spacing)
    {
        if (action->widget()) {
            return action->widget()->size();
        }

        const QString &text = action->text();
        QSize icon_size = action->iconSize();

        if (!icon_size.isValid() && !action->icon().isNull()) {
            icon_size = fallbackIconSize;
        }

        QSize max_size;

        max_size.setWidth(max.width() >= 0 ? max.width() : QWIDGETSIZE_MAX);
        max_size.setHeight(max.height() >= 0 ? max.height() : QWIDGETSIZE_MAX);

        if (text.isEmpty()) {
            return icon_size.boundedTo(max_size);
        }

        QFontMetrics fm(action->font());
        const QSize &text_size = fm.size(0, text);

        if (!icon_size.isValid())
            spacing = 0;

        return QSize(icon_size.width() + text_size.width() + spacing, qMax(icon_size.height(), text_size.height()));
    }

    static QSize displayActionSize(const DViewItemAction *action, const QStyle *style, const QStyleOptionViewItem &option)
    {
        QStyleOptionViewItem item;
        item.text = action->text();
        item.features = option.features | QStyleOptionViewItem::HasDisplay;
        item.font = action->font();
        item.fontMetrics = QFontMetrics(item.font);
        item.rect = option.rect;
        item.decorationPosition = option.decorationPosition;
        item.decorationSize = option.decorationSize;

        return DStyle::viewItemSize(style, &item, Qt::DisplayRole);
    }

    static QList<QRect> doActionsLayout(QRect base, const DViewItemActionList &list, Qt::Orientation orientation,
                                        Qt::LayoutDirection direction, const QSize &fallbackIconSize, QSize *bounding)
    {
        if (list.isEmpty()) {
            if (bounding) {
                bounding->setWidth(0);
                bounding->setHeight(0);
            }

            return {};
        }

        QList<QRect> result_list;
        int max_width = 0;
        int max_height = 0;
        QList<QSize> size_list;
        size_list.reserve(list.size());
        int spacing = DStyleHelper(qApp->style()).pixelMetric(DStyle::PM_ContentsSpacing);

        if (orientation == Qt::Horizontal) {
            // ??????????????????????????????
            int top_width = 0;
            int center_width = 0;
            int bottom_width = 0;

            // ?????????????????????
            for (const DViewItemAction *action : list) {
                size_list.append(actionSize(action, action->maximumSize(), fallbackIconSize, spacing));

                if (action->alignment().testFlag(Qt::AlignVCenter)) {
                    center_width += size_list.last().width();
                } else if (action->alignment().testFlag(Qt::AlignBottom)) {
                    bottom_width += size_list.last().width();
                } else {
                    top_width += size_list.last().width();
                }
            }

            max_width = qBound(qMax(top_width, center_width), bottom_width, base.width()) + (list.size() -  1) * spacing;
            base.setWidth(max_width);

            QRect top_rect = base;
            QRect center_rect = base;
            QRect bottom_rect = base;

            // ????????????
            for (int i = 0; i < list.size(); ++i) {
                const DViewItemAction *action = list.at(i);

                if (action->alignment().testFlag(Qt::AlignVCenter)) {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), center_rect);
                    center_rect.setLeft(result_list.last().right() + spacing);
                } else if (action->alignment().testFlag(Qt::AlignBottom)) {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), bottom_rect);
                    bottom_rect.setLeft(result_list.last().right() + spacing);
                } else {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), top_rect);
                    top_rect.setLeft(result_list.last().right() + spacing);
                }

                if (bounding)
                    max_height = qMax(max_height, result_list.last().bottom() - base.top() + 1);
            }
        } else {
            // ??????????????????????????????
            int left_height = 0;
            int center_height = 0;
            int right_height = 0;

            // ?????????????????????
            for (const DViewItemAction *action : list) {
                size_list.append(actionSize(action, action->maximumSize(), fallbackIconSize, spacing));

                if (action->alignment().testFlag(Qt::AlignHCenter)) {
                    center_height += size_list.last().height();
                } else if (action->alignment().testFlag(Qt::AlignRight)) {
                    right_height += size_list.last().height();
                } else {
                    left_height += size_list.last().height();
                }
            }

            max_height = qBound(qMax(left_height, center_height), right_height, base.height()) + (list.size() -  1) * spacing;
            base.setHeight(max_height);

            QRect left_rect = base;
            QRect center_rect = base;
            QRect right_rect = base;

            // ????????????
            for (int i = 0; i < list.size(); ++i) {
                const DViewItemAction *action = list.at(i);

                if (action->alignment().testFlag(Qt::AlignHCenter)) {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), center_rect);
                    center_rect.setTop(result_list.last().bottom() + spacing);
                } else if (action->alignment().testFlag(Qt::AlignRight)) {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), right_rect);
                    right_rect.setTop(result_list.last().bottom() + spacing);
                } else {
                    result_list << QStyle::alignedRect(direction, action->alignment(), size_list.at(i), left_rect);
                    left_rect.setTop(result_list.last().bottom() + spacing);
                }

                if (bounding)
                    max_height = qMax(max_height, result_list.last().bottom() - base.top() + 1);
            }
        }

        if (bounding) {
            bounding->setWidth(max_width);
            bounding->setHeight(max_height);
        }

        return result_list;
    }

    static void drawAction(QPainter *pa, const QStyleOptionViewItem &option, const QRect &rect, const DViewItemAction *action, int spacing)
    {
        if (!action->isVisible()) {
            return;
        }

        if (action->widget()) {
            if (action->widget()->geometry() != rect) {
                action->widget()->setGeometry(rect);
            }

            return;
        }

        QSize icon_size(action->iconSize());

        if (!icon_size.isValid() && !action->icon().isNull()) {
            icon_size = option.decorationSize;
        }

        // ????????????????????????????????????????????????
        QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
                              ? QPalette::Normal : QPalette::Disabled;
        if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
            cg = QPalette::Inactive;

        if (option.state & QStyle::State_Selected) {
            pa->setPen(option.palette.color(cg, QPalette::HighlightedText));
        } else {
            if (action->textColorType() > 0) {
                pa->setPen(QPen(DPaletteHelper::instance()->palette(option.widget).brush(cg, action->textColorType()), 1));
            } else {
                QPalette::ColorRole role = action->textColorRole() > 0 ? action->textColorRole() : QPalette::Text;
                pa->setPen(QPen(option.palette.brush(cg, role), 1));
            }
        }

        // draw icon
        if (icon_size.isValid()) {
            QRect icon_rect(QPoint(0, 0), icon_size);

            icon_rect.moveCenter(rect.center());
            icon_rect.moveLeft(rect.left());

            if (action->dciIcon().isNull()) {
                const QIcon &icon = action->icon();
                auto modeStatePair = DStyle::toIconModeState(&option);
                icon.paint(pa, icon_rect, Qt::AlignCenter, modeStatePair.first, modeStatePair.second);
            } else {
                DDciIcon dciicon = action->dciIcon();
                DDciIcon::Mode mode = DStyle::toDciIconMode(&option);
                auto appTheme = DGuiApplicationHelper::toColorType(option.palette);
                DDciIcon::Theme theme = appTheme == DGuiApplicationHelper::LightType ? DDciIcon::Light : DDciIcon::Dark;
                DDciIconPalette palette{option.palette.color(cg, QPalette::WindowText), option.palette.color(cg, QPalette::Window),
                                        option.palette.color(cg, QPalette::Highlight), option.palette.color(cg, QPalette::HighlightedText)};
                if (option.state & QStyle::State_Selected)
                    palette.setForeground(option.palette.color(cg, QPalette::HighlightedText));
                dciicon.paint(pa, icon_rect, pa->device() ? pa->device()->devicePixelRatio()
                                                          : qApp->devicePixelRatio(), theme, mode, Qt::AlignCenter, palette);
            }
        }

        // draw text
        if (!action->text().isEmpty()) {
            QRect text_rect = rect;

            if (!icon_size.isValid()) {
                spacing = 0;
            }

            text_rect.setLeft(text_rect.left() + icon_size.width() + spacing);
            pa->setFont(action->font());
            pa->drawText(text_rect, Qt::AlignVCenter, action->text());
        }
    }

    static QSize drawActions(QPainter *pa, const QStyleOptionViewItem &option, const QVariant &value, Qt::Edge edge, QList<QPair<QAction*, QRect>> *clickableActionRect)
    {
        const DViewItemActionList &actionList = qvariantToActionList(value);
        DViewItemActionList visiable_actionList;
        for (auto action : actionList) {
            if (action->isVisible())
                visiable_actionList.append(action);
            action->setEnabled(option.state & QStyle::State_Enabled);
            if (QWidget *actionWidget = action->widget()) {
                actionWidget->setVisible(action->isVisible());
                actionWidget->setEnabled(option.state & QStyle::State_Enabled);
            }
        }

        const Qt::Orientation orientation = (edge == Qt::TopEdge || edge == Qt::BottomEdge) ? Qt::Vertical : Qt::Horizontal;
        QSize bounding;
        const QList<QRect> &list = doActionsLayout(option.rect, visiable_actionList, orientation, option.direction, option.decorationSize, &bounding);
        QPoint origin(0, 0);
        int spacing = DStyleHelper(qApp->style()).pixelMetric(DStyle::PM_ContentsSpacing);

        switch (edge) {
        case Qt::BottomEdge:
            origin = QPoint(0, option.rect.height() - bounding.height());
            break;
        case Qt::RightEdge:
            origin = QPoint(option.rect.width() - bounding.width(), 0);
            break;
        default:
            break;
        }

        for (int i = 0; i < list.count(); ++i) {
            DViewItemAction *action = visiable_actionList.at(i);
            const QRect &rect = list.at(i).translated(origin);

            drawAction(pa, option, rect, action, spacing);

            if (action->isClickable()) {
                clickableActionRect->append(qMakePair(action, rect.marginsAdded(action->clickAreaMargins())));
            }
        }

        return bounding;
    }

    static DViewItemActionList allActions(const QModelIndex &index)
    {
        static const QVector<ItemDataRole> rules {
            LeftActionListRole,
                    TopActionListRole,
                    RightActionListRole,
                    BottomActionListRole,
                    TextActionListRole
        };
        DViewItemActionList results;
        for (const auto role: rules) {
            const auto &list = qvariantToActionList(index.data(role));
            if (list.isEmpty())
                continue;
            results << list;
        }
        return results;
    }

    bool readyRecordVisibleWidgetOfCurrentFrame()
    {
        // multi QEvent maybe be merged to one using postEvent
        // so we can clear cache avoid to recording multi ItemWidget.
        if (Q_UNLIKELY(hasStartRecord)) {
            currentWidgets.clear();
            return false;
        }
        hasStartRecord = true;
        return true;
    }

    void updateWidgetVisibleInUnvisualArea()
    {
        hasStartRecord = false;
        if (lastWidgets.isEmpty() && currentWidgets.isEmpty())
            return;

        for (const auto &widget : qAsConst(lastWidgets)) {
            if (currentWidgets.contains(widget))
                continue;
            if (widget && widget->isVisible())
                widget->setVisible(false);
        }

        lastWidgets.swap(currentWidgets);
        currentWidgets.clear();
    }

    void recordVisibleWidgetOfCurrentFrame(const QModelIndex &index)
    {
        // only record virsual widget when starting record.
        if (Q_UNLIKELY(!hasStartRecord))
            return;

        for (auto action : allActions(index)) {
            if (!action->isVisible())
                continue;

            if (auto widget = action->widget())
                currentWidgets.append(QPointer<QWidget>(widget));
        }
    }

    DStyledItemDelegate::BackgroundType backgroundType = DStyledItemDelegate::NoBackground;
    QMargins margins;
    QSize itemSize;
    int itemSpacing = 0;
    QMap<QModelIndex, QList<QPair<QAction*, QRect>>> clickableActionMap;
    QAction *pressedAction = nullptr;
    QList<QPointer<QWidget>> lastWidgets;
    QList<QPointer<QWidget>> currentWidgets;
    bool hasStartRecord = false;
};

/*!
  \class Dtk::Widget::DViewItemAction
  \inmodule dtkwidget
  \brief ????????? action ??????, ??????????????????????????????.
  
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  
  setActionList(Qt::BottomEdge, *list)?????????BottomEdge????????????????????????????????????: LeftEdge, RightEdge,???????????????alignment???????????????????????????????????????
  
  ????????? alignment ??????????????????(?????????):
  \list
  \li AlignTop
  \li AlignVCenter
  \li AlignBottom
  \endlist

  ????????????????????????, ??????????????????????????????, ????????????, ????????????:
  \code
  QMainWindow *wid = new QMainWindow();
  wid->resize(800, 600);
  
  //???????????????
  DListView *view = new DListView(wid);
  QStandardItemModel *model = new QStandardItemModel(view);
  view->setModel(model);
  
  //??????DStandardItem item
  DStandardItem *item = new DStandardItem();
  item->setText("@1:???????????????item ???text\n@2:???????????????item ???text\n@3:???????????????item ???text\n@4:???????????????item ???text\n@5:???????????????item ???text\n@6:???????????????item ???text");
  
  //DViewItemAction() ?????????: ????????? alignment = AlignLeft ??? AlignHCenter ???  AlignRight; ????????? AlignTop, AlignVCenter, AlignBottom ???????????????
  DViewItemAction *act1 = new DViewItemAction(Qt::AlignBottom, QSize(15, 15), QSize(50, 50));
  act1->setText("act1");
  act1->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act2 = new DViewItemAction(Qt::AlignVCenter, QSize(15, 15), QSize());
  act2->setText("act2");
  act2->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act3 = new DViewItemAction(Qt::AlignTop, QSize(15, 15), QSize(100, 50));
  act3->setText("act3");
  act3->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act4 = new DViewItemAction(Qt::AlignBottom, QSize(15, 15), QSize(100, 50));
  act4->setText("act4");
  act4->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  
  DViewItemActionList *list = new DViewItemActionList();
  list->append(act1);
  list->append(act2);
  list->append(act3);
  list->append(act4);
  
  //edge??????: TopEdg, BottomEdge (??????????????????????????????), LeftEdge, RightEdge
  item->setActionList(Qt::LeftEdge, *list);
  model->appendRow(item);
  
  wid->setCentralWidget(view);
  wid->show();
  \endcode

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  
  setActionList(Qt::BottomEdge, *list)?????????BottomEdge????????????????????????????????????: TopEdge, BottomEdge,???????????????alignment???????????????????????????????????????
  
  ????????? alignment ??????????????????(?????????):
  \list
  \li AlignLeft
  \li AlignHCenter
  \li AlignRight
  \endlist

  ????????????????????????, ??????????????????????????????, ????????????, ????????????:
  \code
  QMainWindow *wid = new QMainWindow();
  wid->resize(800, 600);
  
  //???????????????
  DListView *view = new DListView(wid);
  QStandardItemModel *model = new QStandardItemModel(view);
  view->setModel(model);
  
  //??????DStandardItem item
  DStandardItem *item = new DStandardItem();
  item->setText("????????????item ???text");
  
  //DViewItemAction() ?????????: ????????? alignment = AlignLeft ??? AlignHCenter ???  AlignRight; ????????? AlignTop, AlignVCenter, AlignBottom ???????????????
  DViewItemAction *act1 = new DViewItemAction(Qt::AlignLeft, QSize(15, 15), QSize(50, 50));
  act1->setText("act1");
  act1->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act2 = new DViewItemAction(Qt::AlignRight, QSize(15, 15), QSize());
  act2->setText("act2");
  act2->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act3 = new DViewItemAction(Qt::AlignHCenter, QSize(15, 15), QSize(100, 50));
  act3->setText("act3");
  act3->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act4 = new DViewItemAction(Qt::AlignLeft, QSize(15, 15), QSize(100, 50));
  act4->setText("act4");
  act4->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  
  DViewItemActionList *list = new DViewItemActionList();
  list->append(act1);
  list->append(act2);
  list->append(act3);
  list->append(act4);
  
  //edge??????: TopEdg, BottomEdge (??????????????????????????????), LeftEdge, RightEdge
  item->setActionList(Qt::BottomEdge, *list);
  model->appendRow(item);
  
  wid->setCentralWidget(view);
  wid->show();
  \endcode
  \a alignment ?????????????????????(???????????????)????????????: ??????, ??????, ?????? (???????????????????????????)
  \a iconSize ?????????????????? icon ?????????
 */

/*!
  \brief ????????????
  \a alignment ?????????????????????
  \a iconSize ???????????? icon ?????????
  \a maxSize ???????????????????????????
  \a clickable ?????????????????????????????????
 */
DViewItemAction::DViewItemAction(Qt::Alignment alignment, const QSize &iconSize, const QSize &maxSize, bool clickable)
    : QAction(nullptr)
    , DObject(*new DViewItemActionPrivate(this))
{
    D_D(DViewItemAction);

    d->alignment = alignment;
    d->iconSize = iconSize.isValid() ? iconSize : QSize(qApp->style()->pixelMetric(DStyle::PM_IndicatorWidth), qApp->style()->pixelMetric(DStyle::PM_IndicatorHeight));
    d->maxSize = maxSize;
    d->clickable = clickable;
}

DViewItemAction::DViewItemAction(Qt::Alignment alignment, const QSize &iconSize,
                                 const QSize &maxSize, bool clickable, QObject *parent)
    : DViewItemAction(alignment, iconSize, maxSize, clickable)
{
    if (parent) {
        qWarning() << "setting parent for a DViewItemAction object is no longer supported.";
    }
}

/*!
  \brief ?????? DViewItemAction ?????????(??????)??????
  \return ?????? DViewItemAction ?????????(??????)??????
 */
Qt::Alignment DViewItemAction::alignment() const
{
    D_DC(DViewItemAction);

    return d->alignment;
}

/*!
  \brief ???????????? icon ???????????????
  \return ???????????? icon ???????????????
 */
QSize DViewItemAction::iconSize() const
{
    D_DC(DViewItemAction);

    return d->iconSize;
}

/*!
  \brief ???????????????????????????
  \return ???????????????????????????
 */
QSize DViewItemAction::maximumSize() const
{
    D_DC(DViewItemAction);

    return d->maxSize;
}

/*!
  \brief ????????????????????????
  \return ????????????????????????
 */
QMargins DViewItemAction::clickAreaMargins() const
{
    D_DC(DViewItemAction);

    return d->clickMargins;
}

/*!
  \brief ????????????????????????(eg: ???????????????icon??????????????????????????????????????? ?????????????????? ???????????????????????????????????????????????????)
  \a margins ???????????????????????????????????????
 */
void DViewItemAction::setClickAreaMargins(const QMargins &margins)
{
    D_D(DViewItemAction);

    d->clickMargins = margins;
}

/*!
  \fn void DViewItemAction::setTextColorRole(DPalette::ColorType role)
  \brief ??????????????????????????????????????? (DPalette)
  \overload
  \a role ????????????????????????
 */
void DViewItemAction::setTextColorRole(DPalette::ColorType role)
{
    D_D(DViewItemAction);

    d->colorRole = -1;
    d->colorType = role;
}

/*!
  \fn void DViewItemAction::setTextColorRole(QPalette::ColorRole role)
  \brief ??????????????????????????????????????? (QPalette)
  \overload
  \a role ????????????????????????
 */
void DViewItemAction::setTextColorRole(QPalette::ColorRole role)
{
    D_D(DViewItemAction);

    d->colorType = -1;
    d->colorRole = role;
}

/*!
  \brief ????????????????????????
  \return ???????????????????????????
 */
DPalette::ColorType DViewItemAction::textColorType() const
{
    D_DC(DViewItemAction);

    return static_cast<DPalette::ColorType>(d->colorType);
}

/*!
  \brief ????????????????????????
  \return ????????????????????????
 */
DPalette::ColorRole DViewItemAction::textColorRole() const
{
    D_DC(DViewItemAction);

    return static_cast<DPalette::ColorRole>(d->colorRole);
}

/*!
  \brief ??????????????????
  \a size ??????????????????????????????
 */
void DViewItemAction::setFontSize(DFontSizeManager::SizeType size)
{
    D_D(DViewItemAction);

    d->fontSize = size;
}

/*!
  \brief ????????????
  \return ????????????
 */
QFont DViewItemAction::font() const
{
    D_DC(DViewItemAction);

    if (d->fontSize < 0) {
        return QAction::font();
    }

    return DFontSizeManager::instance()->get(static_cast<DFontSizeManager::SizeType>(d->fontSize), QAction::font());
}

/*!
  \brief ????????????????????????????????????(?????????????????????????????????true???????????????false)
  \return ????????????????????????????????????
 */
bool DViewItemAction::isClickable() const
{
    D_DC(DViewItemAction);

    return d->clickable;
}

/*!
  \brief ?????? QWidget
  \a widget ?????????????????? QWidget
 */
void DViewItemAction::setWidget(QWidget *widget)
{
    D_D(DViewItemAction);

    d->widget = QPointer<QWidget>(widget);
    d->widget->setVisible(false);
}

/*!
  \brief ?????? QWidget ????????????
  \return ?????? QWidget ????????????
 */
QWidget *DViewItemAction::widget() const
{
    D_DC(DViewItemAction);

    return d->widget;
}

void DViewItemAction::setDciIcon(const DDciIcon &dciIcon)
{
    D_D(DViewItemAction);

    d->dciIcon = dciIcon;
}

DDciIcon DViewItemAction::dciIcon() const
{
    D_DC(DViewItemAction);

    return d->dciIcon;
}

static QPalette::ColorRole getViewItemColorRole(const QModelIndex &index, int role)
{
    const QVariant &value = index.data(role);

    if (!value.isValid())
        return QPalette::NoRole;

    auto pair = qvariant_cast<QPair<int, int>>(value);

    return static_cast<QPalette::ColorRole>(pair.first);
}

static DPalette::ColorType getViewItemColorType(const QModelIndex &index, int role)
{
    const QVariant &value = index.data(role);

    if (!value.isValid())
        return DPalette::NoType;

    auto pair = qvariant_cast<QPair<int, int>>(value);

    return static_cast<DPalette::ColorType>(pair.second);
}

static QFont getViewItemFont(const QModelIndex &index, int role)
{
    const QVariant &value = index.data(role);

    if (!value.isValid()) {
        return qvariant_cast<QFont>(index.data(Qt::FontRole));
    }

    DFontSizeManager::SizeType size = static_cast<DFontSizeManager::SizeType>(qvariant_cast<int>(value));
    return DFontSizeManager::instance()->get(size);
}

DStyledItemDelegate::DStyledItemDelegate(QAbstractItemView *parent)
    : QStyledItemDelegate(parent)
    , DObject(*new DStyledItemDelegatePrivate(this))
{
    //??????QAction?????????
    parent->viewport()->installEventFilter(this);

    // ????????? background type. ?????? setBackgroundType() ????????????????????????????????????????????????????????????????????????
    setBackgroundType(DStyledItemDelegate::RoundedBackground);
}

void DStyledItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    D_DC(DStyledItemDelegate);

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    QRect backup_opt_rect = opt.rect;

    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    bool editing = false;

    if (auto view = qobject_cast<const QListView*>(widget)) {
        editing = !qobject_cast<const QListWidget*>(view) && view->isPersistentEditorOpen(index);
    }

    if (!editing)
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

    // draw the background
    if (!editing && backgroundType() != NoBackground && !(opt.state & QStyle::State_Selected)
            && !(d->backgroundType & NoNormalState && DStyle::getState(&opt) == DStyle::SS_NormalState)) {
        DStyleOptionBackgroundGroup boption;
        boption.init(widget);
        boption.QStyleOption::operator =(opt);
        boption.position = DStyleOptionBackgroundGroup::ItemBackgroundPosition(opt.viewItemPosition);

        if (opt.backgroundBrush.style() != Qt::NoBrush) {
            boption.dpalette.setBrush(DPalette::ItemBackground, opt.backgroundBrush);
        }

        boption.rect = opt.rect;

        if (backgroundType() != RoundedBackground) {
            boption.directions = Qt::Vertical;
        }

        style->drawPrimitive(static_cast<QStyle::PrimitiveElement>(DStyle::PE_ItemBackground), &boption, painter, widget);
    }

    // ??????????????????
    QMargins margins = d->margins;
    const QVariant &margins_varinat = index.data(Dtk::MarginsRole);

    if (margins_varinat.isValid()) {
        margins = qvariant_cast<QMargins>(margins_varinat);
    }

    opt.rect = opt.rect.marginsRemoved(margins);
    QRect itemContentRect = opt.rect;
    QSize action_area_size(0, 0);
    QList<QPair<QAction*, QRect>> clickActionList;
    int spacing = DStyleHelper(qApp->style()).pixelMetric(DStyle::PM_ContentsSpacing);

    action_area_size = d->drawActions(painter, opt, index.data(Dtk::LeftActionListRole), Qt::LeftEdge, &clickActionList);
    itemContentRect.setLeft(itemContentRect.left() + action_area_size.width() + (action_area_size.isNull() ? 0 : spacing));

    action_area_size = d->drawActions(painter, opt, index.data(Dtk::RightActionListRole), Qt::RightEdge, &clickActionList);
    itemContentRect.setRight(itemContentRect.right() - action_area_size.width() - (action_area_size.isNull() ? 0 : spacing));

    action_area_size = d->drawActions(painter, opt, index.data(Dtk::TopActionListRole), Qt::TopEdge, &clickActionList);
    itemContentRect.setTop(itemContentRect.top() + action_area_size.height() + (action_area_size.isNull() ? 0 : spacing));

    action_area_size = d->drawActions(painter, opt, index.data(Dtk::BottomActionListRole), Qt::BottomEdge, &clickActionList);
    itemContentRect.setBottom(itemContentRect.bottom() - action_area_size.height() - (action_area_size.isNull() ? 0 : spacing));

    if (!clickActionList.isEmpty()) {
        const_cast<DStyledItemDelegatePrivate*>(d)->clickableActionMap[index] = clickActionList;
    } else {
        const_cast<DStyledItemDelegatePrivate*>(d)->clickableActionMap.remove(index);
    }

    const DViewItemActionList &text_action_list = qvariantToActionList(index.data(Dtk::TextActionListRole));

    opt.rect = itemContentRect;
    QRect iconRect, textRect, checkRect;
    DStyle::viewItemLayout(style, &opt, &iconRect, &textRect, &checkRect, false);

    // ????????????????????????????????????????????????
    QPalette::ColorGroup cg = opt.state & QStyle::State_Enabled
                          ? QPalette::Normal : QPalette::Disabled;

    if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active))
        cg = QPalette::Inactive;

    if (opt.state & QStyle::State_Selected) {
        painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
    } else {
        painter->setPen(opt.palette.color(cg, QPalette::Text));
    }

    // draw icon
    if (opt.features & QStyleOptionViewItem::HasDecoration) {
        QVariant icon = index.data(Qt::DecorationRole);
        DDciIcon dciIcon;
        if (icon.canConvert<DTK_GUI_NAMESPACE::DDciIcon>())
            dciIcon = qvariant_cast<DDciIcon>(icon);

        if (dciIcon.isNull()) {
            QIcon::Mode mode = QIcon::Normal;
            if (!(opt.state & QStyle::State_Enabled))
                mode = QIcon::Disabled;
            else if (opt.state & QStyle::State_Selected)
                mode = QIcon::Selected;
            QIcon::State state = opt.state & QStyle::State_Open ? QIcon::On : QIcon::Off;
            opt.icon.paint(painter, iconRect, opt.decorationAlignment, mode, state);
        } else {
            DDciIcon::Mode mode = DStyle::toDciIconMode(&option);
            auto appTheme = DGuiApplicationHelper::toColorType(option.palette);
            DDciIcon::Theme theme = appTheme == DGuiApplicationHelper::LightType ? DDciIcon::Light : DDciIcon::Dark;
            DDciIconPalette palette{option.palette.color(cg, QPalette::WindowText), option.palette.color(cg, QPalette::Window),
                                    option.palette.color(cg, QPalette::Highlight), option.palette.color(cg, QPalette::HighlightedText)};
            if (option.state & QStyle::State_Selected)
                palette.setForeground(opt.palette.color(cg, QPalette::HighlightedText));
            dciIcon.paint(painter, iconRect, painter->device() ? painter->device()->devicePixelRatioF()
                                                               : qApp->devicePixelRatio(),
                          theme, mode, Qt::AlignCenter, palette);
        }
    }

    // draw the text
    if (opt.features & QStyleOptionViewItem::HasDisplay) {
        if (opt.state & QStyle::State_Editing) {
            painter->setPen(opt.palette.color(cg, QPalette::Text));
            painter->drawRect(textRect.adjusted(0, 0, -1, -1));
        }

        if (!text_action_list.isEmpty()) {
            opt.displayAlignment &= ~Qt::AlignBottom;
            opt.displayAlignment &= ~Qt::AlignVCenter;
            opt.displayAlignment |= Qt::AlignTop;
        }

        if (text_action_list.isEmpty()) {
            DStyle::viewItemDrawText(style, painter, &opt, textRect);
        } else {
            opt.displayAlignment &= ~Qt::AlignVCenter;
            opt.displayAlignment &= ~Qt::AlignBottom;

            QRect bounding = DStyle::viewItemDrawText(style, painter, &opt, textRect);

            // draw action text
            for (const DViewItemAction *action : text_action_list) {
                textRect.setTop(bounding.bottom());
                opt.font = action->font();
                opt.text = action->text();

                if (opt.state & QStyle::State_Selected) {
                    painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
                } else if (action->textColorType() >= 0) {
                    painter->setPen(DPaletteHelper::instance()->palette(widget).color(cg, action->textColorType()));
                } else if (action->textColorRole() >= 0) {
                    painter->setPen(opt.palette.color(cg, action->textColorRole()));
                } else {
                    painter->setPen(opt.palette.color(cg, QPalette::Text));
                }

                bounding = DStyle::viewItemDrawText(style, painter, &opt, textRect);
            }
        }
    }

    // draw the check mark
    if (opt.features & QStyleOptionViewItem::HasCheckIndicator) {
        QStyleOptionViewItem option(opt);
        option.rect = checkRect;
        option.state = option.state & ~QStyle::State_HasFocus;

        switch (opt.checkState) {
        case Qt::Unchecked:
            option.state |= QStyle::State_Off;
            break;
        case Qt::PartiallyChecked:
            option.state |= QStyle::State_NoChange;
            break;
        case Qt::Checked:
            option.state |= QStyle::State_On;
            break;
        }

        style->drawPrimitive(QStyle::PE_IndicatorViewItemCheck, &option, painter, widget);
    }

    // reset rect for focus rect
    opt.rect = backup_opt_rect;

    if (opt.state & QStyle::State_HasFocus) {
        QStyleOptionFocusRect o;
        o.QStyleOption::operator=(opt);
        o.rect = style->subElementRect(QStyle::SE_ItemViewItemFocusRect, &opt, widget);
        o.state |= QStyle::State_KeyboardFocusChange;
        o.state |= QStyle::State_Item;
        QPalette::ColorGroup cg = (opt.state & QStyle::State_Enabled)
                      ? QPalette::Normal : QPalette::Disabled;
        o.backgroundColor = opt.palette.color(cg, (opt.state & QStyle::State_Selected)
                                              ? QPalette::Highlight : QPalette::Window);
        style->drawPrimitive(QStyle::PE_FrameFocusRect, &o, painter, widget);
    }

    const_cast<DStyledItemDelegatePrivate*>(d)->recordVisibleWidgetOfCurrentFrame(index);
}

QSize DStyledItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    D_DC(DStyledItemDelegate);

    if (d->itemSize.isValid()) {
        return d->itemSize;
    }

    QVariant value = index.data(Qt::SizeHintRole);

    if (value.isValid())
        return qvariant_cast<QSize>(value);

    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    QRect pixmapRect, textRect, checkRect;
    DStyle::viewItemLayout(style, &opt, &pixmapRect, &textRect, &checkRect, true);

    const DViewItemActionList &text_action_list = qvariantToActionList(index.data(Dtk::TextActionListRole));

    for (const DViewItemAction *action : text_action_list) {
        const QSize &action_size = d->displayActionSize(action, style, opt);
        textRect.setWidth(qMax(textRect.width(), action_size.width()));
        textRect.setHeight(textRect.height() + action_size.height());
    }

    QSize size = (pixmapRect | textRect | checkRect).size();

    const DViewItemActionList &left_actions = qvariantToActionList(index.data(Dtk::LeftActionListRole));
    const DViewItemActionList &right_actions = qvariantToActionList(index.data(Dtk::RightActionListRole));
    const DViewItemActionList &top_actions = qvariantToActionList(index.data(Dtk::TopActionListRole));
    const DViewItemActionList &bottom_actions = qvariantToActionList(index.data(Dtk::BottomActionListRole));

    QSize action_area_size;
    // ????????????????????????
    d->doActionsLayout(QRect(0, 0, QWIDGETSIZE_MAX, size.height()), left_actions, Qt::Horizontal,
                       option.direction, option.decorationSize, &action_area_size);
    size.setHeight(qMax(size.height(), action_area_size.height()));
    size.setWidth(size.width() + action_area_size.width());

    // ????????????????????????
    d->doActionsLayout(QRect(0, 0, QWIDGETSIZE_MAX, size.height()), right_actions, Qt::Horizontal,
                       option.direction, option.decorationSize, &action_area_size);
    size.setHeight(qMax(size.height(), action_area_size.height()));
    size.setWidth(size.width() + action_area_size.width());

    // ????????????????????????
    d->doActionsLayout(QRect(0, 0, size.width(), QWIDGETSIZE_MAX), top_actions, Qt::Vertical,
                       option.direction, option.decorationSize, &action_area_size);
    size.setHeight(size.height() + action_area_size.height());
    size.setWidth(qMax(size.width(), action_area_size.width()));

    // ????????????????????????
    d->doActionsLayout(QRect(0, 0, size.width(), QWIDGETSIZE_MAX), bottom_actions, Qt::Vertical,
                       option.direction, option.decorationSize, &action_area_size);
    size.setHeight(size.height() + action_area_size.height());
    size.setWidth(qMax(size.width(), action_area_size.width()));

    QMargins margins = d->margins;
    const QVariant &margins_varinat = index.data(Dtk::MarginsRole);

    if (margins_varinat.isValid()) {
        margins = qvariant_cast<QMargins>(margins_varinat);
    }

    // ???item????????????????????????????????????spacing
    const QListView * lv = qobject_cast<const QListView*>(option.widget);
    if (lv) {
        if (lv->flow() == QListView::LeftToRight) {
            size.rwidth() += d->itemSpacing;
        } else {
            size.rheight() += d->itemSpacing;
        }
    }

    return QRect(QPoint(0, 0), size).marginsAdded(margins).size();
}

void DStyledItemDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!editor)
        return;
    Q_ASSERT(index.isValid());
    const QWidget *widget = option.widget;

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    QRect rect = opt.rect;

    if (opt.features & QStyleOptionViewItem::HasDecoration) {
        opt.showDecorationSelected = true;

        QStyle *style = widget ? widget->style() : QApplication::style();
        QRect geom = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, widget);
        int spacing = DStyleHelper(style).pixelMetric(DStyle::PM_ContentsSpacing, &opt, widget);

        // ????????????????????????????????????????????????????????????
        switch (opt.decorationPosition) {
        case QStyleOptionViewItem::Left:
            rect.setLeft(geom.right() + spacing);
            break;
        case QStyleOptionViewItem::Right:
            rect.setRight(geom.left() - spacing);
            break;
        case QStyleOptionViewItem::Top:
            rect.setTop(geom.bottom() + spacing);
            break;
        case QStyleOptionViewItem::Bottom:
            rect.setBottom(geom.top() - spacing);
            break;
        default:
            break;
        }
    }

    // ??????????????????????????????????????????
    rect.setHeight(qMin(rect.height(), editor->sizeHint().height()));

    editor->setGeometry(rect);
}

DStyledItemDelegate::BackgroundType DStyledItemDelegate::backgroundType() const
{
    D_DC(DStyledItemDelegate);
    return DStyledItemDelegate::BackgroundType(d->backgroundType & BackgroundType_Mask);
}

QMargins DStyledItemDelegate::margins() const
{
    D_DC(DStyledItemDelegate);

    return d->margins;
}

QSize DStyledItemDelegate::itemSize() const
{
    D_DC(DStyledItemDelegate);

    return d->itemSize;
}

int DStyledItemDelegate::spacing() const
{
    D_DC(DStyledItemDelegate);

    return d->itemSpacing;
}

void DStyledItemDelegate::setBackgroundType(DStyledItemDelegate::BackgroundType type)
{
    D_D(DStyledItemDelegate);

    if (d->backgroundType == type)
        return;

    d->backgroundType = type;
    d->margins = QMargins();

    if (backgroundType() != NoBackground) {
        QStyle *style = qApp->style();

        if (QWidget *w = qobject_cast<QWidget *>(parent())) {
            style = w->style();
        }

        int frame_margin = style->pixelMetric(static_cast<QStyle::PixelMetric>(DStyle::PM_FrameRadius));
        d->margins += frame_margin;
    }
}

void DStyledItemDelegate::setMargins(const QMargins margins)
{
    D_D(DStyledItemDelegate);

    d->margins = margins;
}

void DStyledItemDelegate::setItemSize(QSize itemSize)
{
    D_D(DStyledItemDelegate);

    d->itemSize = itemSize;
}

void DStyledItemDelegate::setItemSpacing(int spacing)
{
    D_D(DStyledItemDelegate);

    d->itemSpacing = spacing;
}

void DStyledItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    QVariant value = index.data(Qt::DecorationRole);
    if (value.canConvert<DDciIcon>()) {
        // The dciicon can not be set to opt.icon
        auto dciIcon = qvariant_cast<DDciIcon>(value);
        DDciIcon::Mode mode;
        if (!(option->state & QStyle::State_Enabled))
            mode = DDciIcon::Disabled;
        else if (option->state & QStyle::State_Selected)
            mode = DDciIcon::Pressed;
        else
            mode = DDciIcon::Normal;
        auto appTheme = DGuiApplicationHelper::toColorType(option->palette);
        DDciIcon::Theme theme = appTheme == DGuiApplicationHelper::LightType ? DDciIcon::Light : DDciIcon::Dark;
        int actualSize = dciIcon.actualSize(option->decorationSize.width(), theme, mode);
        // For highdpi icons actualSize might be larger than decorationSize, which we don't want. Clamp it to decorationSize.
        option->decorationSize = QSize(qMin(option->decorationSize.width(), actualSize),
                                       qMin(option->decorationSize.height(), actualSize));
    }

    if (option->viewItemPosition == QStyleOptionViewItem::ViewItemPosition::Invalid) {
        const int rowCount =  index.model()->rowCount();
        if (rowCount == 1) {
            option->viewItemPosition = QStyleOptionViewItem::ViewItemPosition::OnlyOne;
        } else if (index.row() == 0) {
            option->viewItemPosition = QStyleOptionViewItem::ViewItemPosition::Beginning;
        } else if (index.row() == rowCount - 1) {
            option->viewItemPosition = QStyleOptionViewItem::ViewItemPosition::End;
        } else {
            option->viewItemPosition = QStyleOptionViewItem::ViewItemPosition::Middle;
        }
    }

    if (index.data(Dtk::TextActionListRole).isValid()) {
        option->features |= QStyleOptionViewItem::HasDisplay;
    }

    if ((option->state & (QStyle::State_HasFocus | QStyle::State_Selected)) == (QStyle::State_HasFocus | QStyle::State_Selected)) {
        if (const QAbstractItemView *view = qobject_cast<const QAbstractItemView*>(option->widget)) {
            // item ???????????????????????????????????????border??????????????????????????????????????????????????????
            if (view->selectionMode() == QAbstractItemView::SingleSelection) {
                option->state &= ~QStyle::State_HasFocus;
            }
        }
    }

    D_DC(DStyledItemDelegate);
    const QListView * lv = qobject_cast<const QListView*>(option->widget);
    if (lv) {
        if (lv->flow() == QListView::LeftToRight) {
            option->rect.adjust(0, 0, 0 - d->itemSpacing, 0);
        } else {
            option->rect.adjust(0, 0, 0, 0 - d->itemSpacing);
        }
        if (lv->window() && lv->window()->isActiveWindow()) {
            option->state |= QStyle::State_Active;
        } else {
            option->state &= (~QStyle::State_Active);
        }
    }

    DPalette::ColorType type = getViewItemColorType(index, Dtk::ViewItemForegroundRole);

    if (type != DPalette::NoType) {
        option->palette.setBrush(QPalette::Text, DPaletteHelper::instance()->palette(option->widget).brush(type));
    } else {
        QPalette::ColorRole role = getViewItemColorRole(index, Dtk::ViewItemForegroundRole);

        if (role != QPalette::NoRole) {
            option->palette.setBrush(QPalette::Text, lv->palette().brush(role));
        }
    }

    type = getViewItemColorType(index, Dtk::ViewItemBackgroundRole);

    if (type != DPalette::NoType) {
        option->backgroundBrush = DPaletteHelper::instance()->palette(option->widget).brush(type);
    } else {
        QPalette::ColorRole role = getViewItemColorRole(index, Dtk::ViewItemBackgroundRole);

        if (role != QPalette::NoRole) {
            option->backgroundBrush = lv->palette().brush(role);
        }
    }

    option->font = getViewItemFont(index, Dtk::ViewItemFontLevelRole);
}

bool DStyledItemDelegate::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        D_D(DStyledItemDelegate);
        d->pressedAction = nullptr;
        Q_FALLTHROUGH();
    }
    case QEvent::MouseButtonRelease: {
        QMouseEvent *ev = static_cast<QMouseEvent*>(event);
        D_D(DStyledItemDelegate);

        QAbstractItemView *view = qobject_cast<QAbstractItemView*>(parent());
        const QModelIndex &index = view->indexAt(ev->pos());

        for (auto action_map : d->clickableActionMap.value(index)) {
            if (action_map.first->isEnabled()
                    && action_map.second.contains(ev->pos(), true)) {
                if (event->type() == QEvent::MouseButtonRelease
                        && d->pressedAction == action_map.first) {
                    action_map.first->trigger();
                } else {
                    d->pressedAction = action_map.first;
                }

                return true;
            }
        }
        break;
    }
    default:
        break;
    }
    const auto view = qobject_cast<QAbstractItemView*>(parent());
    if (view && object == view->viewport()) {
        static const QEvent::Type UpdateWidgetVisibleEvent(
                    static_cast<QEvent::Type>(QEvent::registerEventType()));

        if (event->type() == QEvent::Paint) {
            D_D(DStyledItemDelegate);
            if (d->readyRecordVisibleWidgetOfCurrentFrame()) {
                auto updateEvent = new QEvent(UpdateWidgetVisibleEvent);
                qApp->postEvent(view->viewport(), updateEvent);
            }
        } else if (event->type() == UpdateWidgetVisibleEvent) {
            D_D(DStyledItemDelegate);
            d->updateWidgetVisibleInUnvisualArea();
            return true;
        }
    }

    return QStyledItemDelegate::eventFilter(object, event);
}

static Dtk::ItemDataRole getActionPositionRole(Qt::Edge edge)
{
    switch (edge) {
    case Qt::TopEdge:
        return Dtk::TopActionListRole;
    case Qt::RightEdge:
        return Dtk::RightActionListRole;
    case Qt::LeftEdge:
        return Dtk::LeftActionListRole;
    case Qt::BottomEdge:
        return Dtk::BottomActionListRole;
    default:
        break;
    }

    return Dtk::LeftActionListRole;
}

/*!
  \class Dtk::Widget::DStandardItem
  \inmodule dtkwidget
  \brief ??????????????? item,??????????????????/??????,?????????-??????-????????????,????????????????????? item ??????.

  \sa QStandardItem DViewItemAction
 */

/*!
  \brief ????????????
 */
DStandardItem::~DStandardItem()
{
}

/*!
  \brief ?????? actionList
  \a edge ??????????????????????????????, ???????????? list ??????????????? DStandardItem ?????????????????????
                  edge???????????? item ?????????????????????????????????????????? item ?????????????????????????????????????????????????????? setIcon???setText?????????????????????????????????
  \a list ?????? actiontem ???????????? list ??????
 */
void DStandardItem::setActionList(Qt::Edge edge, const DViewItemActionList &list)
{
    QVariant value;

    if (!list.isEmpty()) {
        value = actionListToQVariant(list);;
    }

    auto role = getActionPositionRole(edge);
    setData(value, role);
}

/*!
  \brief ????????? item ??????????????? list
  \a edge edge???????????? item ?????????????????????????????????????????? item ?????????????????????????????????????????????????????? setIcon???setText?????????????????????????????????
  \return ????????? item ??????????????? list
 */
DViewItemActionList DStandardItem::actionList(Qt::Edge edge) const
{
    return qvariantToActionList(data(getActionPositionRole(edge)));
}

/*!
  \brief ????????? item ??????????????? \a list, ???????????? text ??? DViewItemActionList ??????
   ??? item ??????????????? \a list, ?????????????????? item ?????????
  \b ????????????????????????????????? setTextActionList():
  
  \code
  QMainWindow *wid = new QMainWindow();
  wid->resize(800, 600);
  
  //???????????????
  DListView *view = new DListView(wid);
  QStandardItemModel *model = new QStandardItemModel(view);
  view->setModel(model);
  
  //??????DStandardItem item
  DStandardItem *item = new DStandardItem();
  item->setText("@1:???????????????item ???text\n@2:???????????????item ???text\n@3:???????????????item ???text\n@4:???????????????item ???text\n@5:???????????????item ???text\n@6:???????????????item ???text");
  
  //DViewItemAction() ?????????: ????????? alignment = AlignLeft ??? AlignHCenter ???  AlignRight; ????????? AlignTop, AlignVCenter, AlignBottom ???????????????
  DViewItemAction *act1 = new DViewItemAction(Qt::AlignBottom, QSize(15, 15), QSize(50, 50));
  act1->setText("act1");
  act1->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act2 = new DViewItemAction(Qt::AlignVCenter, QSize(15, 15), QSize());
  act2->setText("act2");
  act2->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act3 = new DViewItemAction(Qt::AlignTop, QSize(15, 15), QSize(100, 50));
  act3->setText("act3");
  act3->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  DViewItemAction *act4 = new DViewItemAction(Qt::AlignBottom, QSize(15, 15), QSize(100, 50));
  act4->setText("act4");
  act4->setIcon(QIcon("/home/yuanyi/Desktop/dog.jpg"));
  
  DViewItemActionList *list = new DViewItemActionList();
  list->append(act1);
  list->append(act2);
  list->append(act3);
  list->append(act4);
  
  item->setTextActionList(*list);
  model->appendRow(item);
  
  wid->setCentralWidget(view);
  wid->show();
  \endcode
 */
void DStandardItem::setTextActionList(const DViewItemActionList &list)
{
    QVariant value;

    if (!list.isEmpty()) {
        value = actionListToQVariant(list);;
    }

    setData(value, Dtk::TextActionListRole);
}

/*!
  \brief ?????? "???????????????????????????" ??????
 */
DViewItemActionList DStandardItem::textActionList() const
{
    return qvariantToActionList(data(Dtk::TextActionListRole));
}

void DStandardItem::setTextColorRole(DPalette::ColorType role)
{
    setData(QVariant::fromValue(qMakePair((int)QPalette::NoRole, (int)role)), Dtk::ViewItemForegroundRole);
}

void DStandardItem::setTextColorRole(QPalette::ColorRole role)
{
    setData(QVariant::fromValue(qMakePair((int)role, (int)DPalette::NoType)), Dtk::ViewItemForegroundRole);
}

DPalette::ColorType DStandardItem::textColorType() const
{
    return getViewItemColorType(index(), Dtk::ViewItemForegroundRole);
}

QPalette::ColorRole DStandardItem::textColorRole() const
{
    return getViewItemColorRole(index(), Dtk::ViewItemForegroundRole);
}

void DStandardItem::setBackgroundRole(DPalette::ColorType role)
{
    setData(QVariant::fromValue(qMakePair((int)QPalette::NoRole, (int)role)), Dtk::ViewItemBackgroundRole);
}

void DStandardItem::setBackgroundRole(QPalette::ColorRole role)
{
    setData(QVariant::fromValue(qMakePair((int)role, (int)DPalette::NoType)), Dtk::ViewItemBackgroundRole);
}

DPalette::ColorType DStandardItem::backgroundType() const
{
    return getViewItemColorType(index(), Dtk::ViewItemBackgroundRole);
}

QPalette::ColorRole DStandardItem::backgroundRole() const
{
    return getViewItemColorRole(index(), Dtk::ViewItemBackgroundRole);
}

void DStandardItem::setFontSize(DFontSizeManager::SizeType size)
{
    setData((int)size, Dtk::ViewItemFontLevelRole);
}

QFont DStandardItem::font() const
{
    return getViewItemFont(index(), Dtk::ViewItemFontLevelRole);
}

void DStandardItem::setDciIcon(const DDciIcon &dciIcon)
{
    setData(QVariant::fromValue<DDciIcon>(dciIcon), Qt::DecorationRole);
}

DDciIcon DStandardItem::dciIcon() const
{
    return qvariant_cast<DDciIcon>(data(Qt::DecorationRole));
}

QStandardItem *DStandardItem::clone() const
{
    return new DStandardItem(*this);
}

DWIDGET_END_NAMESPACE
