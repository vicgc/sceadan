SUBDIRS = doc src tools liblinear-1.94-omp
ACLOCAL_AMFLAGS = -I m4
DISTCLEANFILES = mp.d penmp.d

.PHONY: download
download:
	(cd src;$(MAKE) download)
	./configure

.PHONY: pull
pull:
	git pull
