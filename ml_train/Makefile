PYTHON ?= ./venv/Scripts/python.exe
PIPELINE_ARGS ?= --window 5 --hidden-sizes 8 --epochs 30
SWEEP_ARGS ?= --epochs 100 --early-stop-patience 5 --early-stop-min-delta 0.001
PLAY_ARGS ?=

.PHONY: help install compile pipeline sweep play export-controller test-controller test controller-runner

help:
	@echo Available targets:
	@echo   make install
	@echo   make compile
	@echo   make pipeline
	@echo   make sweep
	@echo   make play
	@echo   make export-controller
	@echo   make test-controller
	@echo   make test
	@echo   make controller-runner

install:
	$(PYTHON) -m pip install -r requirements.txt

compile:
	$(PYTHON) -m compileall ml scripts tests

pipeline:
	$(PYTHON) scripts/run_pipeline.py $(PIPELINE_ARGS)

sweep:
	$(PYTHON) scripts/sweep_configs.py $(SWEEP_ARGS)

play:
	$(PYTHON) scripts/play_quantized.py $(PLAY_ARGS)

export-controller:
	$(PYTHON) scripts/export_controller_weights.py --model-path artifacts/sweep/work/w6_h16/model_quantized.npz --output-dir controller

test-controller:
	$(PYTHON) tests/test_controller_export.py

test: compile test-controller

controller-runner:
	wsl gcc -std=c99 -Wall -Wextra -Werror -I controller controller/quant_model.c controller/model_weights.c controller/model_runner.c -o /tmp/mic32_model_runner
	wsl /tmp/mic32_model_runner
