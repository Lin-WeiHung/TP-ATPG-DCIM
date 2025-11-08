#pragma once

#include <QMainWindow>
#include <QFutureWatcher>
#include <memory>

class QLineEdit;
class QPushButton;
class QComboBox;
class QTextEdit;
class QLabel;
class QProgressBar;
class QListWidget;
class QListView;
class QTableView;
class QSlider;
class QUndoStack;

class ElementsModel;
class OpsModel;
class MarchModel;
class LookAheadService;

struct GuiSimInput;
struct GuiSimOutput;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBrowseFaults();
    void onBrowseMarch();
    void onMarchPathChanged();
    void onRun();
    void onSimFinished();
    void onElementSelectionChanged();
    void onInsertW0();
    void onInsertW1();
    void onInsertR0();
    void onInsertR1();
    void onInsertC();
    void onApplySelectedCandidate();
    void recomputeLookAhead();

private:
    void buildUi();
    void setUiEnabled(bool enabled);
    void appendLog(const QString& text);
    void populateMarchTests();
    void connectModels();
    void refreshModels();

    // UI widgets
    QLineEdit* faultsPathEdit_{};
    QPushButton* browseFaultsBtn_{};
    QLineEdit* marchPathEdit_{};
    QPushButton* browseMarchBtn_{};
    QComboBox* testSelect_{};
    QPushButton* runBtn_{};
    QPushButton* saveHtmlBtn_{};
    QLabel* covStateLbl_{};
    QLabel* covSensLbl_{};
    QLabel* covDetectLbl_{};
    QLabel* covTotalLbl_{};
    QLabel* opsCountLbl_{};
    QTextEdit* logEdit_{};
    QProgressBar* progress_{};
    // Left/Mid/Right widgets
    QListView* elementsList_{};
    QTableView* opsTable_{};
    QListWidget* step1List_{};
    QPushButton* applyCandidateBtn_{};
    QSlider* alphaSlider_{}; QSlider* betaSlider_{}; QSlider* gammaSlider_{}; QSlider* lambdaSlider_{};
    // Compute op controls
    QComboBox* cT_{}; QComboBox* cM_{}; QComboBox* cB_{};

    // Async run
    std::unique_ptr<QFutureWatcher<std::shared_ptr<GuiSimOutput>>> watcher_;

    // Data
    std::shared_ptr<GuiSimInput> input_;
    std::shared_ptr<GuiSimOutput> lastOut_;

    // Models
    std::unique_ptr<MarchModel> marchModel_;
    std::unique_ptr<ElementsModel> elementsModel_;
    std::unique_ptr<OpsModel> opsModel_;
    std::unique_ptr<QUndoStack> undoStack_;
    std::unique_ptr<LookAheadService> lookSvc_;
};

