#pragma once

#include <QDialog>
#include <glm/glm.hpp>

#include "UVGenerator.h"
#include "AssImpModelLoader.h"

namespace Ui
{
    class UVGenerationDialog;
}


class UVGenerationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UVGenerationDialog(QWidget* parent = nullptr);
    ~UVGenerationDialog();

    // Get the selected UV method
    UVMethod getSelectedMethod() const;

    // Get the configured UV parameters
    UVConfig getUVConfig() const;

    // Set initial values (optional - for editing existing settings)
    void setMethod(UVMethod method);
    void setConfig(const UVConfig& config);

    QString getMethodName(UVMethod method) const;

protected:
    void accept() override;

private slots:
    void onMethodChanged(int index);
    void onRelaxationToggled(bool enabled);
    void onRelaxationToggled_Smart(bool enabled);
    void onCylAutoDetectAxisToggled(bool autoDetect);
    void onSphereAutoDetectAxisToggled(bool autoDetect);
    void onTorusAutoDetectAxisToggled(bool autoDetect);


private:
    // Helper methods
    void setupConnections();
    void updateOptionsPage(int methodIndex);
    void adjustDialogSize();  // Auto-resize based on content

	void loadLastUsedSettings();
	void saveLastUsedSettings();

private:
    Ui::UVGenerationDialog* ui;
};
