#ifndef WIDG_H
#define WIDG_H

#include <QWidget>

#include "module/signaling_client.h"

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
    SignalingClient* m_ptrSignalingClient{nullptr};
};

#endif // WIDG_H
