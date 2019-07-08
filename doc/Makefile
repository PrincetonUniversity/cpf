PAPER = paper
TEX = $(wildcard *.tex)
BIB = references.bib
FIGS = $(wildcard figures/*.pdf figures/*.png graphs/*.pdf graphs/*.png)

.PHONY: all clean

$(PAPER).pdf: $(TEX) $(BIB) $(FIGS) jpaper.cls
	echo $(FIGS)
	echo $(TEX)
	pdflatex -output-format pdf $(PAPER).tex
	bibtex $(PAPER)
	pdflatex -output-format pdf $(PAPER).tex
	pdflatex -output-format pdf $(PAPER).tex

clean:
	rm -f *.aux *.bbl *.blg *.log *.out $(PAPER).pdf

