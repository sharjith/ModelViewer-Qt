#include "BackgroundColor.h"
#include "ui_BackgroundColor.h"

#include "GLWidget.h"

#include <QColorDialog>
#include <QMessageBox>

BackgroundColor::BackgroundColor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BackgroundColor)
{
    ui->setupUi(this);

    GLWidget* glWidget = dynamic_cast<GLWidget*>(parent);
    if(glWidget)
    {        
        topColor = glWidget->getBgTopColor();
        QPalette pal = ui->pushButtonTop->palette();
        pal.setColor(QPalette::Button, topColor);
        ui->pushButtonTop->setAutoFillBackground(true);
        ui->pushButtonTop->setPalette(pal);
        ui->pushButtonTop->update();

        pal = ui->pushButtonBottom->palette();
        bottomColor = glWidget->getBgBotColor();
        pal.setColor(QPalette::Button, bottomColor);
        ui->pushButtonBottom->setAutoFillBackground(true);
        ui->pushButtonBottom->setPalette(pal);
        ui->pushButtonBottom->update();
    }
}

BackgroundColor::~BackgroundColor()
{
    delete ui;
}

bool BackgroundColor::hasGradient() const
{
    return ui->checkBoxGrad->isChecked();
}

void BackgroundColor::applyBgColors()
{
    GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
    if(glWidget)
    {
        QPalette pal = ui->pushButtonTop->palette();
        QColor topColor = pal.color(QPalette::Button);
        glWidget->setBgTopColor(topColor);
        if(hasGradient())
        {
            pal = ui->pushButtonBottom->palette();
            QColor botColor = pal.color(QPalette::Button);
            glWidget->setBgBotColor(botColor);
        }
        else
            glWidget->setBgBotColor(topColor);
    }
}

void BackgroundColor::on_okButton_clicked()
{
    applyBgColors();
    QDialog::accept();
}

void BackgroundColor::on_applyButton_clicked()
{
    applyBgColors();
}

void BackgroundColor::on_cancelButton_clicked()
{
    QDialog::reject();
}

void BackgroundColor::on_pushButtonTop_clicked()
{
    QColor initColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f, 1.0f);
    topColor = QColorDialog::getColor(initColor, this);
    if(topColor.isValid())
    {
        GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
        if(glWidget)
        {
            initColor = glWidget->getBgTopColor();

            QPalette pal = ui->pushButtonTop->palette();
            pal.setColor(QPalette::Button, topColor);
            ui->pushButtonTop->setAutoFillBackground(true);
            ui->pushButtonTop->setPalette(pal);
            ui->pushButtonTop->update();
        }
    }
}

void BackgroundColor::on_pushButtonBottom_clicked()
{
    QColor initColor  = QColor::fromRgbF(0.925f, 0.913f, 0.847f, 1.0f);
    bottomColor = QColorDialog::getColor(initColor, this);
    if(bottomColor.isValid())
    {
        GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
        if(glWidget)
        {
            initColor = glWidget->getBgBotColor();

            QPalette pal = ui->pushButtonBottom->palette();
            pal.setColor(QPalette::Button, bottomColor);
            ui->pushButtonBottom->setAutoFillBackground(true);
            ui->pushButtonBottom->setPalette(pal);
            ui->pushButtonBottom->update();
        }
    }
}

void BackgroundColor::on_pushButtonDefaultColor_clicked()
{
    QColor col = QColor::fromRgbF(0.3f, 0.3f, 0.3f, 1.0f);
    QPalette pal = ui->pushButtonTop->palette();
    pal.setColor(QPalette::Button, col);
    ui->pushButtonTop->setAutoFillBackground(true);
    ui->pushButtonTop->setPalette(pal);
    ui->pushButtonTop->update();

    col = QColor::fromRgbF(0.925f, 0.913f, 0.847f, 1.0f);
    pal = ui->pushButtonBottom->palette();
    pal.setColor(QPalette::Button, col);
    ui->pushButtonBottom->setAutoFillBackground(true);
    ui->pushButtonBottom->setPalette(pal);
    ui->pushButtonBottom->update();
}


