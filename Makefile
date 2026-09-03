.PHONY: format format-check

format:
	bash scripts/clang_format_21.sh --apply --all

format-check:
	bash scripts/clang_format_21.sh --check --all
