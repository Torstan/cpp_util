SUBDIRS := computational_geometry concurrent_queue

.PHONY: all test clean $(SUBDIRS)

all test clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit $$?; \
	done

$(SUBDIRS):
	$(MAKE) -C $@ all
