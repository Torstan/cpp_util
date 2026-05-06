SUBDIRS := computational_geometry concurrent_queue

.PHONY: all test clean lint $(SUBDIRS)

all test clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit $$?; \
	done

lint:
	@for dir in $(SUBDIRS); do \
		$(MAKE) --no-print-directory -C $$dir lint || exit $$?; \
	done

$(SUBDIRS):
	$(MAKE) -C $@ all
