.PHONY: clean All

All:
	@echo "----------Building project:[ tool - Debug ]----------"
	@cd "tool" && "$(MAKE)" -f  "tool.mk"
clean:
	@echo "----------Cleaning project:[ tool - Debug ]----------"
	@cd "tool" && "$(MAKE)" -f  "tool.mk" clean
