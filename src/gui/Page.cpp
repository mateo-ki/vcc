#include "gui/Page.h"

#include "app/AppContext.h"

namespace Gui {

Page::Page(App::AppContext &context, QWidget *parent) : QWidget(parent), context_(context)
{
    setAttribute(Qt::WA_StyledBackground, true);
}

}
