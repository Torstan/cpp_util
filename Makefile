SUBDIRS := computational_geometry concurrent_queue

.PHONY: all test clean $(SUBDIRS)

all:
	$(MAKE) -C computational_geometry all
	$(MAKE) -C concurrent_queue all

test:
	$(MAKE) -C computational_geometry test
	$(MAKE) -C concurrent_queue test

clean:
	$(MAKE) -C computational_geometry clean
	$(MAKE) -C concurrent_queue clean

$(SUBDIRS):
	$(MAKE) -C $@ all
