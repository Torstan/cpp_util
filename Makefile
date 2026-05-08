SUBDIRS := computational_geometry concurrent_queue immutable_container

.PHONY: all test clean lint test-lint $(SUBDIRS)

all test clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit $$?; \
	done

lint:
	@for dir in $(SUBDIRS); do \
		$(MAKE) --no-print-directory -C $$dir lint || exit $$?; \
	done

test-lint:
	@bash scripts/test_lint_wrappers.sh

$(SUBDIRS):
	$(MAKE) -C $@ all
