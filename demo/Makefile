COMAC = /usr/bin/cm4all-comac --quiet-output

COMA_SOURCES = $(wildcard demo/coma/*.cma)
COMA_CLASSES = $(patsubst %.cma,%.cls,$(COMA_SOURCES))

demo-all: $(COMA_CLASSES)

demo-clean:
	rm -f $(COMA_CLASSES)

$(COMA_CLASSES): %.cls: %.cma
	$(COMAC) $<
