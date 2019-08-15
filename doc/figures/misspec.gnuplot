# set terminal pdf enhanced font "helvetica,10" fontscale 1.0 size 1280, 720
set terminal pdf enhanced font "helvetica,7" fontscale 1.0
set output "misspec.pdf"

reset

set linetype 8 lw 1 lc rgb "gray" dashtype "-"

set key inside box
set style increment default

set noxtics
set ytics norangelimit autofreq font ",8" format "%.0fx"
set xtics border in scale 0,0 nomirror autojustify font "helvetica,6.5"
# set xtics norangelimit font ",8"
# set xtics ()
set style data histograms
# set style histogram cluster title textcolor lt -1 offset character 0, -1
set style histogram cluster gap 1
# set xlabel offset character 0, -2, 0 font "Helvetica,8" textcolor lt -1 norotate
set ylabel "Speedup over sequential" font "helvetica,8"

set style fill solid border rgb "black"
set grid noxtics ytics linetype 8
set grid
# set boxwidth 0.8

set yrange [0:40]
# set title "Misspeculation Performance" font "helvetica,10"
plot 'misspec.dat' using 1:xtic(4) title 'No Misspec' linecolor rgb "#2c7bb6", \
                '' using 2 title '\~0\.1% Misspec' linecolor rgb "#d7191c"
