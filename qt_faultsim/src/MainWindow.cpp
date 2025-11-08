#include "MainWindow.hpp"

#include <QWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QListView>
#include <QTableView>
#include <QSlider>
#include <QUndoStack>
#include <QtConcurrent>

#include <fstream>
#include <sstream>
#include <memory>
#include <optional>

#include "../../include/FaultSimulator.hpp"
#include "domain/MarchModel.hpp"
#include "models/ElementsModel.hpp"
#include "models/OpsModel.hpp"
#include "services/LookAheadService.hpp"

using std::string; using std::vector;

struct GuiSimInput {
    string faults_json;
    string march_json;
    int selected_mt_index{-1};
};

struct GuiSimOutput {
    // parsed
    vector<RawFault> raw_faults;
    vector<Fault> faults;
    vector<TestPrimitive> all_tps;
    vector<RawMarchTest> raw_mts;
    vector<MarchTest> march_tests;

    // result for selected test
    int mt_index{-1};
    SimulationResult sim;

    // timing
    long long t_faults_us{0};
    long long t_march_us{0};
    long long t_sim_us{0};

    string error;
};

static std::shared_ptr<GuiSimOutput> run_simulation(const GuiSimInput& in) {
    using clock = std::chrono::steady_clock;
    auto to_us = [](auto dur){ return std::chrono::duration_cast<std::chrono::microseconds>(dur).count(); };
    auto out = std::make_shared<GuiSimOutput>();
    try {
        FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;
        auto t1s = clock::now();
        out->raw_faults = fparser.parse_file(in.faults_json);
        out->faults.reserve(out->raw_faults.size());
        for (const auto& rf : out->raw_faults) {
            try { out->faults.push_back(fnorm.normalize(rf)); }
            catch(const std::exception&){ /* skip bad faults */ }
        }
        for (const auto& f : out->faults) {
            auto tps = tpg.generate(f);
            out->all_tps.insert(out->all_tps.end(), tps.begin(), tps.end());
        }
        auto t1e = clock::now(); out->t_faults_us = to_us(t1e - t1s);

        MarchTestJsonParser mparser; MarchTestNormalizer mnorm;
        auto t2s = clock::now();
        out->raw_mts = mparser.parse_file(in.march_json);
        out->march_tests.reserve(out->raw_mts.size());
        for (const auto& r : out->raw_mts) out->march_tests.push_back(mnorm.normalize(r));
        auto t2e = clock::now(); out->t_march_us = to_us(t2e - t2s);

        if (in.selected_mt_index < 0 || in.selected_mt_index >= (int)out->march_tests.size()) {
            throw std::runtime_error("Invalid March test index");
        }
        FaultSimulator simulator;
        auto t3s = clock::now();
        out->mt_index = in.selected_mt_index;
        out->sim = simulator.simulate(out->march_tests.at(out->mt_index), out->faults, out->all_tps);
        auto t3e = clock::now(); out->t_sim_us = to_us(t3e - t3s);

        return out;
    } catch (const std::exception& e) {
        out->error = e.what();
        return out;
    }
}

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent) {
    buildUi();
    marchModel_ = std::make_unique<MarchModel>(this);
    elementsModel_ = std::make_unique<ElementsModel>(marchModel_.get(), this);
    opsModel_ = std::make_unique<OpsModel>(marchModel_.get(), this);
    undoStack_ = std::make_unique<QUndoStack>(this);
    lookSvc_ = std::make_unique<LookAheadService>();
    connectModels();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi(){
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // Paths row
    {
        auto* grid = new QGridLayout();
        int r = 0;
        grid->addWidget(new QLabel("faults.json:"), r, 0);
        faultsPathEdit_ = new QLineEdit(central);
        faultsPathEdit_->setText("input/fault.json");
        browseFaultsBtn_ = new QPushButton("Browse…", central);
        grid->addWidget(faultsPathEdit_, r, 1);
        grid->addWidget(browseFaultsBtn_, r, 2);
        connect(browseFaultsBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseFaults);
        r++;
        grid->addWidget(new QLabel("MarchTest.json:"), r, 0);
        marchPathEdit_ = new QLineEdit(central);
        marchPathEdit_->setText("input/MarchTest.json");
        browseMarchBtn_ = new QPushButton("Browse…", central);
        grid->addWidget(marchPathEdit_, r, 1);
        grid->addWidget(browseMarchBtn_, r, 2);
        connect(browseMarchBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseMarch);
        connect(marchPathEdit_, &QLineEdit::editingFinished, this, &MainWindow::onMarchPathChanged);
        root->addLayout(grid);
    }

    // Test selection and actions
    {
        auto* h = new QHBoxLayout();
        h->addWidget(new QLabel("March test:"));
        testSelect_ = new QComboBox(central);
        h->addWidget(testSelect_, 1);
        runBtn_ = new QPushButton("Run Simulation", central);
        saveHtmlBtn_ = new QPushButton("Save HTML…", central);
        saveHtmlBtn_->setEnabled(false);
        h->addWidget(runBtn_);
        h->addWidget(saveHtmlBtn_);
        root->addLayout(h);
        connect(runBtn_, &QPushButton::clicked, this, &MainWindow::onRun);
        // saveHtmlBtn_ reserved for later
    }

    // Three-column editor + Coverage panel
    {
        auto* editorRow = new QHBoxLayout();
        // Left: Elements
        {
            auto* v = new QVBoxLayout();
            v->addWidget(new QLabel("Elements"));
            elementsList_ = new QListView(central);
            v->addWidget(elementsList_, 1);
            auto* tools = new QHBoxLayout();
            auto addUp = new QPushButton("+", central);
            auto del = new QPushButton("-", central);
            tools->addWidget(addUp); tools->addWidget(del);
            v->addLayout(tools);
            editorRow->addLayout(v, 1);
            // selection signal will be connected after setting model
            connect(addUp, &QPushButton::clicked, [this]{ marchModel_->addElement(AddrOrder::Any); });
            connect(del, &QPushButton::clicked, [this]{ int r=elementsList_->currentIndex().row(); if(r>=0) marchModel_->removeElement(r); });
        }
        // Middle: Ops table + palette
        {
            auto* v = new QVBoxLayout();
            v->addWidget(new QLabel("Ops"));
            opsTable_ = new QTableView(central);
            v->addWidget(opsTable_, 1);
            auto* palette = new QVBoxLayout();
            // Row 1: W/R palette
            {
                auto* row = new QHBoxLayout();
                auto bW0 = new QPushButton("W0"); auto bW1 = new QPushButton("W1");
                auto bR0 = new QPushButton("R0"); auto bR1 = new QPushButton("R1");
                row->addWidget(bW0); row->addWidget(bW1);
                row->addWidget(bR0); row->addWidget(bR1);
                palette->addLayout(row);
                connect(bW0, &QPushButton::clicked, this, &MainWindow::onInsertW0);
                connect(bW1, &QPushButton::clicked, this, &MainWindow::onInsertW1);
                connect(bR0, &QPushButton::clicked, this, &MainWindow::onInsertR0);
                connect(bR1, &QPushButton::clicked, this, &MainWindow::onInsertR1);
            }
            // Row 2: Compute palette C(T/M/B)
            {
                auto* row = new QHBoxLayout();
                row->addWidget(new QLabel("C(T/M/B):"));
                cT_ = new QComboBox(central); cT_->addItems({"0","1"}); cT_->setCurrentIndex(0);
                cM_ = new QComboBox(central); cM_->addItems({"0","1"}); cM_->setCurrentIndex(0);
                cB_ = new QComboBox(central); cB_->addItems({"0","1"}); cB_->setCurrentIndex(0);
                row->addWidget(cT_); row->addWidget(cM_); row->addWidget(cB_);
                auto bC = new QPushButton("Insert C");
                row->addWidget(bC);
                connect(bC, &QPushButton::clicked, this, &MainWindow::onInsertC);
                palette->addLayout(row);
            }
            v->addLayout(palette);
            editorRow->addLayout(v, 2);
        }
        // Right: Look-ahead (Step1) + Weights + Coverage
        {
            auto* v = new QVBoxLayout();
            v->addWidget(new QLabel("Look-ahead Step 1"));
            step1List_ = new QListWidget(central);
            v->addWidget(step1List_, 1);
            applyCandidateBtn_ = new QPushButton("Apply selected candidate", central);
            v->addWidget(applyCandidateBtn_);
            connect(applyCandidateBtn_, &QPushButton::clicked, this, &MainWindow::onApplySelectedCandidate);

            // Weights sliders
            auto* grid = new QGridLayout(); int r=0;
            grid->addWidget(new QLabel("alpha_S"), r,0); alphaSlider_=new QSlider(Qt::Horizontal,central); alphaSlider_->setRange(0,300); alphaSlider_->setValue(100); grid->addWidget(alphaSlider_, r,1); r++;
            grid->addWidget(new QLabel("beta_D"), r,0); betaSlider_=new QSlider(Qt::Horizontal,central); betaSlider_->setRange(0,500); betaSlider_->setValue(200); grid->addWidget(betaSlider_, r,1); r++;
            grid->addWidget(new QLabel("gamma_M50"), r,0); gammaSlider_=new QSlider(Qt::Horizontal,central); gammaSlider_->setRange(0,300); gammaSlider_->setValue(50); grid->addWidget(gammaSlider_, r,1); r++;
            grid->addWidget(new QLabel("lambda_M100"), r,0); lambdaSlider_=new QSlider(Qt::Horizontal,central); lambdaSlider_->setRange(0,300); lambdaSlider_->setValue(100); grid->addWidget(lambdaSlider_, r,1); r++;
            v->addLayout(grid);
            auto recomputeConn = [this]{ recomputeLookAhead(); };
            connect(alphaSlider_, &QSlider::valueChanged, this, recomputeConn);
            connect(betaSlider_,  &QSlider::valueChanged, this, recomputeConn);
            connect(gammaSlider_, &QSlider::valueChanged, this, recomputeConn);
            connect(lambdaSlider_,&QSlider::valueChanged, this, recomputeConn);

            // Coverage summary
            auto* cov = new QGridLayout();
            int cr=0;
            cov->addWidget(new QLabel("Ops:"), cr,0); opsCountLbl_ = new QLabel("-", central); cov->addWidget(opsCountLbl_, cr,1); cr++;
            cov->addWidget(new QLabel("State:"), cr,0); covStateLbl_ = new QLabel("-", central); cov->addWidget(covStateLbl_, cr,1); cr++;
            cov->addWidget(new QLabel("Sens:"),  cr,0); covSensLbl_  = new QLabel("-", central); cov->addWidget(covSensLbl_,  cr,1); cr++;
            cov->addWidget(new QLabel("Detect:"),cr,0); covDetectLbl_= new QLabel("-", central); cov->addWidget(covDetectLbl_, cr,1); cr++;
            cov->addWidget(new QLabel("Total:"), cr,0); covTotalLbl_ = new QLabel("-", central); cov->addWidget(covTotalLbl_, cr,1); cr++;
            v->addLayout(cov);

            editorRow->addLayout(v, 2);
        }

        root->addLayout(editorRow, 1);
    }

    // Progress + Log
    progress_ = new QProgressBar(central);
    progress_->setRange(0, 0);
    progress_->setVisible(false);
    root->addWidget(progress_);

    logEdit_ = new QTextEdit(central);
    logEdit_->setReadOnly(true);
    root->addWidget(logEdit_, 1);

    setCentralWidget(central);

    // Initially populate tests
    populateMarchTests();
}

void MainWindow::appendLog(const QString& text){
    logEdit_->append(text);
}

void MainWindow::setUiEnabled(bool enabled){
    faultsPathEdit_->setEnabled(enabled);
    browseFaultsBtn_->setEnabled(enabled);
    marchPathEdit_->setEnabled(enabled);
    browseMarchBtn_->setEnabled(enabled);
    testSelect_->setEnabled(enabled);
    runBtn_->setEnabled(enabled);
}

void MainWindow::onBrowseFaults(){
    QString f = QFileDialog::getOpenFileName(this, "Select faults.json", QString(), "JSON (*.json)");
    if (!f.isEmpty()) faultsPathEdit_->setText(f);
}

void MainWindow::onBrowseMarch(){
    QString f = QFileDialog::getOpenFileName(this, "Select MarchTest.json", QString(), "JSON (*.json)");
    if (!f.isEmpty()) { marchPathEdit_->setText(f); onMarchPathChanged(); }
}

void MainWindow::populateMarchTests(){
    testSelect_->clear();
    try {
        MarchTestJsonParser mparser;
        auto raws = mparser.parse_file(marchPathEdit_->text().toStdString());
        for (const auto& r : raws) testSelect_->addItem(QString::fromStdString(r.name));
        if (raws.empty()) testSelect_->addItem("<no tests found>");
    } catch (const std::exception& e) {
        testSelect_->addItem(QString("Error: %1").arg(e.what()));
    }
}

void MainWindow::onMarchPathChanged(){
    populateMarchTests();
}

void MainWindow::onRun(){
    auto faults = faultsPathEdit_->text().trimmed();
    auto march  = marchPathEdit_->text().trimmed();
    if (faults.isEmpty() || march.isEmpty()) {
        QMessageBox::warning(this, "Missing input", "Please select faults.json and MarchTest.json");
        return;
    }
    int idx = testSelect_->currentIndex();
    if (idx < 0) idx = 0;

    input_ = std::make_shared<GuiSimInput>();
    input_->faults_json = faults.toStdString();
    input_->march_json = march.toStdString();
    input_->selected_mt_index = idx;

    setUiEnabled(false);
    progress_->setVisible(true);
    appendLog("Starting simulation…");

    watcher_ = std::make_unique<QFutureWatcher<std::shared_ptr<GuiSimOutput>>>();
    connect(watcher_.get(), &QFutureWatcher<std::shared_ptr<GuiSimOutput>>::finished, this, &MainWindow::onSimFinished);
    auto fut = QtConcurrent::run([in=*input_](){ return run_simulation(in); });
    watcher_->setFuture(fut);
}

void MainWindow::onSimFinished(){
    progress_->setVisible(false);
    setUiEnabled(true);

    auto out = watcher_->result();
    if (!out) { appendLog("No result returned."); return; }
    if (!out->error.empty()) {
        appendLog(QString("Error: %1").arg(QString::fromStdString(out->error)));
        QMessageBox::critical(this, "Simulation error", QString::fromStdString(out->error));
        return;
    }

    opsCountLbl_->setText(QString::number((int)out->sim.op_table.size()));
    auto pct = [](double v){ return QString::number(v*100.0, 'f', 2) + "%"; };
    covStateLbl_->setText(pct(out->sim.state_coverage));
    covSensLbl_->setText(pct(out->sim.sens_coverage));
    covDetectLbl_->setText(pct(out->sim.detect_coverage));
    covTotalLbl_->setText(pct(out->sim.total_coverage));

    // Populate editor model with the selected March test
    lastOut_ = out;
    if (!out->march_tests.empty() && out->mt_index>=0 && out->mt_index < (int)out->march_tests.size()) {
        marchModel_->test() = out->march_tests[out->mt_index];
        marchModel_->setSelectedElement(marchModel_->elementCount()>0?0:-1);
        refreshModels();
        recomputeLookAhead();
    }

    appendLog(QString("Done. parsed faults→TPs: %1 us, parsed March: %2 us, simulated: %3 us")
              .arg(out->t_faults_us).arg(out->t_march_us).arg(out->t_sim_us));
}

void MainWindow::connectModels(){
    elementsList_->setModel(elementsModel_.get());
    connect(elementsList_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onElementSelectionChanged);
    opsTable_->setModel(opsModel_.get());
}

void MainWindow::refreshModels(){
    opsModel_->setElement(marchModel_->selectedElement());
}

void MainWindow::onElementSelectionChanged(){
    int r = elementsList_->currentIndex().row();
    marchModel_->setSelectedElement(r);
    opsModel_->setElement(r);
    recomputeLookAhead();
}

void MainWindow::onInsertW0(){ if (marchModel_->selectedElement()<0) return; Op o; o.kind=OpKind::Write; o.value=Val::Zero; marchModel_->insertOp(marchModel_->selectedElement(), marchModel_->cursorOp(), o); recomputeLookAhead(); }
void MainWindow::onInsertW1(){ if (marchModel_->selectedElement()<0) return; Op o; o.kind=OpKind::Write; o.value=Val::One;  marchModel_->insertOp(marchModel_->selectedElement(), marchModel_->cursorOp(), o); recomputeLookAhead(); }
void MainWindow::onInsertR0(){ if (marchModel_->selectedElement()<0) return; Op o; o.kind=OpKind::Read;  o.value=Val::Zero; marchModel_->insertOp(marchModel_->selectedElement(), marchModel_->cursorOp(), o); recomputeLookAhead(); }
void MainWindow::onInsertR1(){ if (marchModel_->selectedElement()<0) return; Op o; o.kind=OpKind::Read;  o.value=Val::One;  marchModel_->insertOp(marchModel_->selectedElement(), marchModel_->cursorOp(), o); recomputeLookAhead(); }
void MainWindow::onInsertC(){ if (marchModel_->selectedElement()<0) return; Op o; o.kind=OpKind::ComputeAnd; 
    auto toVal=[&](QComboBox* cb){ return (cb && cb->currentIndex()==1)? Val::One : Val::Zero; };
    o.C_T = toVal(cT_); o.C_M = toVal(cM_); o.C_B = toVal(cB_);
    marchModel_->insertOp(marchModel_->selectedElement(), marchModel_->cursorOp(), o); recomputeLookAhead(); }

static QString opToLabel(const Op& op){
    auto b=[](Val v){ return v==Val::Zero?"0": v==Val::One?"1":"-"; };
    if (op.kind==OpKind::Write) return QString("W%1").arg(op.value==Val::Zero?"0":"1");
    if (op.kind==OpKind::Read)  return QString("R%1").arg(op.value==Val::Zero?"0":"1");
    return QString("C(%1)(%2)(%3)").arg(b(op.C_T), b(op.C_M), b(op.C_B));
}

void MainWindow::recomputeLookAhead(){
    step1List_->clear();
    if (!lastOut_) return;
    if (marchModel_->selectedElement()<0) return;
    LookAheadRequest req; req.base = marchModel_->test(); req.elemIndex = marchModel_->selectedElement(); req.insertPos = marchModel_->cursorOp(); req.maxSteps=1; req.tps = lastOut_->all_tps; req.faults = lastOut_->faults;
    ScoreWeights w; w.alpha_S = alphaSlider_? alphaSlider_->value()/100.0 : 1.0; w.beta_D = betaSlider_? betaSlider_->value()/100.0 : 2.0; w.gamma_MPart = gammaSlider_? gammaSlider_->value()/100.0 : 0.5; w.lambda_MAll = lambdaSlider_? lambdaSlider_->value()/100.0 : 1.0; req.weights = w;
    auto steps = lookSvc_->evaluate(req);
    if (steps.empty()) return;
    for (const auto& c : steps[0].candidates){
        step1List_->addItem(QString("%1  score=%2  S=%3 D=%4 M50=%5 M100=%6")
            .arg(opToLabel(c.candidate))
            .arg(QString::number(c.cumulative_score,'f',4))
            .arg(QString::number(c.outcome.S_cov,'f',2))
            .arg(c.outcome.D_cov)
            .arg(c.outcome.part_M_num)
            .arg(c.outcome.full_M_num));
    }
}

void MainWindow::onApplySelectedCandidate(){
    int row = step1List_->currentRow();
    if (row<0) return;
    // Recompute to align with list order, then apply the op at row index
    LookAheadRequest req; req.base = marchModel_->test(); req.elemIndex = marchModel_->selectedElement(); req.insertPos = marchModel_->cursorOp(); req.maxSteps=1; req.tps = lastOut_? lastOut_->all_tps : std::vector<TestPrimitive>{}; req.faults = lastOut_? lastOut_->faults : std::vector<Fault>{};
    auto steps = lookSvc_->evaluate(req);
    if (steps.empty() || row >= (int)steps[0].candidates.size()) return;
    marchModel_->insertOp(req.elemIndex, req.insertPos, steps[0].candidates[row].candidate);
    recomputeLookAhead();
}
