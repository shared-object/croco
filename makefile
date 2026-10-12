EXAMPLES = chat fileio

all:
	@for d in $(EXAMPLES); do $(MAKE) -C examples/$$d || exit 1; done

clean:
	@for d in $(EXAMPLES); do $(MAKE) -C examples/$$d clean; done

.PHONY: all clean
