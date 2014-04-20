include config.mk

MD_SRC = tabster.c
MD_OBJ = ${MD_SRC:.c=.o}

all: options tabster

options:
	@echo build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"
	@echo

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${MD_OBJ}: config.mk
${MC_OBJ}: config.mk

tabster: ${MD_OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${MD_OBJ} ${LDFLAGS}
	@echo

clean:
	@echo cleaning
	@rm -f tabster ${MD_OBJ} ${MC_OBJ}

install:
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f tabster ${DESTDIR}${PREFIX}/bin

uninstall:
	rm ${DESTDIR}${PREFIX}/bin/tabster
