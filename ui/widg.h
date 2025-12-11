#ifndef WIDG_H
#define WIDG_H

#include <QWidget>
#include "module/SimplePeer.h"

namespace Ui
{
    class widg;
}

class widg : public QWidget
{
    Q_OBJECT

public:
    explicit widg(QWidget *parent = nullptr);
    ~widg();

private:
    Ui::widg *ui;
    SimplePeer* simplePeer;
};

#endif // WIDG_H
