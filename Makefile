NODE_GYP=node node_modules/node-gyp/bin/node-gyp

all: node

node: node_v46

build = stat node_modules/node-gyp > /dev/null 2>&1 || npm install; \
        $(NODE_GYP) clean; \
        printf "%s\nBuilding for %s v%s\n%s\n" "`printf '*%.0s' {1..78}`" "$(1)" "$(3)" "`printf '*%.0s' {1..78}`"; \
        stat ~/.node-gyp/$(3) || ( \
            rm -f /tmp/$(1)-v$(3).tar.gz && \
            curl -o /tmp/$(1)-v$(3).tar.gz https://$(2)/dist/v$(3)/$(1)-v$(3).tar.gz && \
            $(NODE_GYP) install --target=$(3) --tarball=/tmp/$(1)-v$(3).tar.gz --dist-url=http://$(2)/dist && \
            rm -f /tmp/$(1)-v$(3).tar.gz \
        ); \
        $(NODE_GYP) configure --target=$(3) && \
            $(NODE_GYP) build node_ios_device

# Node.js 4.x
node_v46:
	$(call build,node,nodejs.org,4.2.1)

clean:
	$(NODE_GYP) clean
	rm -rf out

.PHONY: clean node_v46
