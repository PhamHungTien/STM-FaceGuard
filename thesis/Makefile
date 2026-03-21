# ============================================================
#  Makefile — Compile LaTeX thesis với XeLaTeX + Biber
# ============================================================
MAIN    = main
LATEX   = xelatex
BIBER   = biber
FLAGS   = -interaction=nonstopmode -synctex=1

.PHONY: all clean

all:
	$(LATEX) $(FLAGS) $(MAIN)
	$(BIBER) $(MAIN)
	$(LATEX) $(FLAGS) $(MAIN)
	$(LATEX) $(FLAGS) $(MAIN)

clean:
	rm -f *.aux *.bbl *.bcf *.blg *.log *.lot *.lof \
	      *.out *.run.xml *.synctex.gz *.toc
