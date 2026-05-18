# MIC32 neural game experiment

Проект обучает MLP предсказывать следующий ход игрока в игре `rock/paper/scissors` на синтетических данных.

## Запуск

Через Makefile:

```powershell
make install
make pipeline
```

Параметры можно переопределять:

```powershell
make pipeline PIPELINE_ARGS="--window 5 --hidden-sizes 8 --epochs 30"
make sweep SWEEP_ARGS="--epochs 100 --early-stop-patience 5 --early-stop-min-delta 0.001"
```

Прямой запуск без `make`:

```powershell
.\venv\Scripts\python.exe -m pip install -r requirements.txt
.\venv\Scripts\python.exe scripts\run_pipeline.py --epochs 30
```

Параметры эксперимента можно менять:

```powershell
.\venv\Scripts\python.exe scripts\run_pipeline.py --window 5 --hidden-sizes 8
```

Сравнение нескольких конфигураций:

```powershell
.\venv\Scripts\python.exe scripts\sweep_configs.py --epochs 100 --early-stop-patience 5 --early-stop-min-delta 0.001
```

## Игра против квантованной модели

```powershell
make play
.\venv\Scripts\python.exe scripts\play_quantized.py
```

Скрипт сам определяет размер истории из модели. После warmup int8-модель предсказывает следующий ход игрока, а AI ходит контрходом.

## Экспорт для контроллера

Текущие embedded controller weights: `w6_h16`, архитектура `36 -> 16 -> 3`, raw weights+biases `643` bytes.

```powershell
make export-controller
make test-controller
.\venv\Scripts\python.exe scripts\export_controller_weights.py --model-path artifacts\sweep\work\w6_h16\model_quantized.npz --output-dir controller
.\venv\Scripts\python.exe tests\test_controller_export.py
```

C inference лежит отдельно от весов: `controller/quant_model.c/.h`. Сгенерированные веса лежат в `controller/model_weights.c/.h`.

## Что делает pipeline

1. Генерирует `data/train.csv`, `data/val.csv`, `data/test.csv` по партиям.
2. Обучает float-модель `30 -> 8 -> 3`.
3. Сохраняет `artifacts/model_float.pth` и метрики.
4. Квантизует веса и bias в int8 с `SCALE=64`, `SHIFT=6`.
5. Сравнивает Random, Repeat-last-opponent, Neural float, Neural quantized.
6. Рисует графики в `artifacts/` и пишет отчет `docs/results.md`.
