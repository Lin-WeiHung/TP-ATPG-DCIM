# 系統架構圖 (Mermaid)

本文件包含 FaultSimulationEvent 與 Greedy Template Search 系統的架構圖，使用 Mermaid 格式繪製。

---

## 1. FaultSimulationEvent 系統架構

```mermaid
flowchart TB
    subgraph FaultSimulationEvent系統
        subgraph 輸入解析層
            FaultsJSON[("Faults.json")]
            MarchTestJSON[("MarchTest.json")]
            FaultsJSON --> FaultsJsonParser
            FaultsJsonParser --> FaultNormalizer
            MarchTestJSON --> MarchTestJsonParser
        end

        subgraph 資料處理層
            FaultNormalizer --> TPGenerator
            TPGenerator --> TPs["TestPrimitive[]"]
            MarchTestJsonParser --> MarchTestNormalizer
            MarchTestNormalizer --> OpTableBuilder
        end

        subgraph 模擬引擎層["FaultSimulatorEvent"]
            OpTableBuilder --> StateCoverEngine["StateCoverEngine\n(729-LUT)"]
            TPs --> StateCoverEngine
            StateCoverEngine --> SensEngine
            SensEngine --> DetectEngine
        end

        subgraph 輸出層
            DetectEngine --> TPEventCenter["TPEventCenter\n(Event Tracking)"]
        end
    end

    style StateCoverEngine fill:#e1f5fe
    style SensEngine fill:#e8f5e9
    style DetectEngine fill:#fff3e0
    style TPEventCenter fill:#fce4ec
```

---

## 2. Cross-Shape 五單元架構

```mermaid
flowchart TB
    A0["A0 (Top)"]
    A1["A1 (Left)"]
    A2["A2_CAS (Center)"]
    A3["A3 (Right)"]
    A4["A4 (Bottom)"]

    A0 --> A2
    A1 --> A2
    A3 --> A2
    A2 --> A4

    style A2 fill:#ffeb3b,stroke:#f57c00,stroke-width:3px
    style A0 fill:#e3f2fd
    style A1 fill:#e3f2fd
    style A3 fill:#e3f2fd
    style A4 fill:#e3f2fd
```

**說明**：
- `A2_CAS` 是受測單元 (Cell Under Test)
- `A0`, `A4` 是同列的上下鄰居 (用於跨列 CFds 故障偵測)
- `A1`, `A3` 是同行的左右鄰居 (用於同列 CFds 故障偵測)

---

## 3. TPEvent 事件狀態機

```mermaid
stateDiagram-v2
    [*] --> Stated: State Cover

    Stated --> StateMasked: 狀態遮蔽
    Stated --> Sensitized: 致敏成功
    Stated --> Detected: R_has_value\n(直接偵測)

    Sensitized --> SensMasked: 致敏遮蔽
    Sensitized --> Detected: 偵測成功

    Detected --> DetectMasked: 偵測後遮蔽

    StateMasked --> [*]
    SensMasked --> [*]
    DetectMasked --> [*]
    Detected --> [*]
```

---

## 4. 三階段流水線

```mermaid
flowchart LR
    subgraph Stage1["Stage 1: State"]
        S1["狀態匹配\n(729-LUT)"]
    end
    
    subgraph Stage2["Stage 2: Sensitize"]
        S2["致敏操作\n序列匹配"]
    end
    
    subgraph Stage3["Stage 3: Detect"]
        S3["偵測操作\n匹配"]
    end

    S1 -->|state_tps| S2
    S2 -->|sens_end| S3
    S3 -->|detected| Output["覆蓋結果"]

    S1 -.->|StateMasked| Masked1["遮蔽"]
    S2 -.->|SensMasked| Masked2["遮蔽"]
    S3 -.->|DetectMasked| Masked3["遮蔽"]

    style Stage1 fill:#e3f2fd
    style Stage2 fill:#e8f5e9
    style Stage3 fill:#fff3e0
```

---

## 5. Greedy Template Search 系統架構

```mermaid
flowchart TB
    subgraph GreedyTemplateSearch系統
        subgraph 模板層
            TemplateLibrary["TemplateLibrary\n(模板庫)"]
            ValueExpandingGen["ValueExpandingGenerator\n(展開生成器)"]
            TemplateLibrary --> ValueExpandingGen
            ValueExpandingGen --> Candidates["MarchElement[]\n(候選元素)"]
        end

        subgraph 約束層
            ConstraintSet["ConstraintSet\n(約束集合)"]
            Candidates --> AllowFilter{"allow()\n過濾"}
            ConstraintSet --> AllowFilter
            AllowFilter --> ValidCandidates["有效候選元素"]
        end

        subgraph 模擬層["FaultSimulator (黑盒子)"]
            ValidCandidates --> Simulator
            Simulator["simulate()"]
            Simulator --> SimResult["SimulationResult\n• state_coverage\n• total_coverage\n• cover_lists"]
        end

        subgraph 評分層
            SimResult --> ScoreFunc["ScoreFunc\n(評分函數)"]
            ScoreFunc --> Score["score = Δstate + Δtotal\n         - ops - Δmask"]
        end

        subgraph 選擇層
            Score --> GreedySelect["Greedy Selection\n(貪婪選擇)"]
            GreedySelect --> BestElem["最高分候選元素"]
        end
    end

    style Simulator fill:#e0e0e0,stroke:#9e9e9e,stroke-width:2px,stroke-dasharray: 5 5
    style ScoreFunc fill:#fff9c4
    style GreedySelect fill:#c8e6c9
```

---

## 6. Greedy 搜尋流程

```mermaid
flowchart TD
    Start([開始]) --> LoadFaults["載入 Faults JSON\n並正規化"]
    LoadFaults --> GenTPs["生成所有\nTestPrimitive"]
    GenTPs --> BuildLib["建立 TemplateLibrary\n(暴力枚舉有效模板)"]
    BuildLib --> InitConstraints["初始化約束集合\n+ FirstElementWriteOnly\n+ DataReadPolarity"]
    InitConstraints --> InitPos["pos = 0"]
    
    InitPos --> GreedyLoop

    subgraph GreedyLoop["Greedy 迴圈"]
        ForTemplate["對每個模板 tid"]
        ForTemplate --> Expand["展開為候選元素"]
        Expand --> ForCandidate["對每個候選"]
        ForCandidate --> CheckConstraint{"約束檢查\nallow()?"}
        CheckConstraint -->|通過| Simulate["模擬 → 評分"]
        CheckConstraint -->|不通過| ForCandidate
        Simulate --> UpdateBest["更新 best"]
        UpdateBest --> ForCandidate
        ForCandidate --> ForTemplate
        ForTemplate --> CommitBest["提交 best_elem\n更新 prefix_state"]
        CommitBest --> IncPos["pos++"]
    end

    IncPos --> CheckPos{"pos < L ?"}
    CheckPos -->|是| ForTemplate
    CheckPos -->|否| Output["輸出最佳結果\nCandidateResult"]
    Output --> End([結束])
```

---

## 7. 模板值展開流程

```mermaid
flowchart LR
    subgraph Template["ElementTemplate"]
        Slot1["Slot 1\nWrite"]
        Slot2["Slot 2\nRead"]
        Slot3["Slot 3\nCompute"]
    end

    Template --> Expand["ValueExpandingGenerator"]

    subgraph Expansion["展開結果 (32種)"]
        E1["W0, R0, C(0)(0)(0)"]
        E2["W1, R0, C(0)(0)(0)"]
        E3["W0, R1, C(0)(0)(0)"]
        E4["..."]
        E5["W1, R1, C(1)(1)(1)"]
    end

    Expand --> Expansion

    Note["展開數 = 2^(r+w+3c)\n= 2^(1+1+3) = 32"]
```

---

## 8. 約束系統架構

```mermaid
classDiagram
    class ISequenceConstraint {
        <<interface>>
        +allow(prefix, elem, pos) bool
        +update(prefix, elem, pos) void
    }

    class FirstElementWriteOnlyConstraint {
        +allow(prefix, elem, pos) bool
    }

    class DataReadPolarityConstraint {
        +allow(prefix, elem, pos) bool
        +update(prefix, elem, pos) void
    }

    class SequenceConstraintSet {
        -constraints_ : vector~ISequenceConstraint~
        +add(constraint) void
        +allow(prefix, elem, pos) bool
        +update(prefix, elem, pos) void
    }

    class PrefixState {
        +D : Val
        +length : size_t
    }

    ISequenceConstraint <|.. FirstElementWriteOnlyConstraint
    ISequenceConstraint <|.. DataReadPolarityConstraint
    SequenceConstraintSet o-- ISequenceConstraint
    SequenceConstraintSet ..> PrefixState : uses
```

---

## 9. 評分函數權重效果

```mermaid
quadrantChart
    title 評分參數權衡空間
    x-axis 低操作懲罰 --> 高操作懲罰
    y-axis 低覆蓋權重 --> 高覆蓋權重
    quadrant-1 高覆蓋短序列
    quadrant-2 高覆蓋長序列
    quadrant-3 低覆蓋長序列
    quadrant-4 低覆蓋短序列
    推薦設定: [0.7, 0.8]
    激進短序列: [0.9, 0.4]
    完整覆蓋: [0.3, 0.95]
```

---

## 10. FaultSimulator 黑盒子視角

```mermaid
flowchart LR
    subgraph 輸入
        MT["MarchTest"]
        Faults["Fault[]"]
        TPs["TestPrimitive[]"]
    end

    subgraph BlackBox["FaultSimulator 🔲"]
        direction TB
        Internal["內部實作\n(不關心)"]
    end

    subgraph 輸出
        Result["SimulationResult"]
        SC["state_coverage"]
        TC["total_coverage"]
        CL["cover_lists\n(含 masked)"]
    end

    MT --> BlackBox
    Faults --> BlackBox
    TPs --> BlackBox
    BlackBox --> Result
    Result --> SC
    Result --> TC
    Result --> CL

    style BlackBox fill:#424242,color:#fff
```

---

*文件版本：1.0*  
*最後更新：2025-12-22*
