// Prueba de integracion a mano: pega contra padelfip.com y matchscorerlive de
// verdad y muestra lo que le llegaria al aparato. No es un test automatico
// porque depende de la red y del calendario del dia.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/padel"
)

// escribirFixture deja el mismo contenido en el formato de lineas que lee el
// simulador. Iterar la pantalla de padel con nombres y largos de verdad es la
// unica forma de que el simulador no mienta.
func escribirFixture(ruta string, s *padel.Snapshot) error {
	var b strings.Builder
	b.WriteString("# fixture de padel para el simulador. Generado con:\n")
	b.WriteString("#   go run ./cmd/padelcheck -fixture ../sim/data/padel.txt\n")
	b.WriteString("# L|nombre|cat|ciudad|pais|rango|faltan|dia|dias|fecha  torneo en juego\n")
	b.WriteString("# M|hora|cancha|fase|gen|a1|a2|b1|b2|seedA|seedB|resA|resB|estado\n")
	b.WriteString("# T|nombre|cat|ciudad|pais|rango|faltan            proximo torneo\n")

	if s.Live != nil {
		fmt.Fprintf(&b, "L|%s|%s|%s|%s|%s|0|%d|%d|%s\n",
			s.Live.Name, s.Live.Cat, s.Live.City, s.Live.Country, s.Live.Rango,
			s.Day, s.Days, s.Fecha)
	}
	for _, m := range s.Matches {
		est := 0
		switch m.State {
		case "jugando":
			est = 1
		case "listo":
			est = 2
		}
		fmt.Fprintf(&b, "M|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d\n",
			m.Time, m.Court, m.Round, m.Gender, m.A1, m.A2, m.B1, m.B2,
			m.SeedA, m.SeedB, m.ScoreA, m.ScoreB, est)
	}
	for _, t := range s.Next {
		fmt.Fprintf(&b, "T|%s|%s|%s|%s|%s|%d\n",
			t.Name, t.Cat, t.City, t.Country, t.Rango, t.Faltan)
	}
	return os.WriteFile(ruta, []byte(b.String()), 0o644)
}

func main() {
	fixture := flag.String("fixture", "", "ademas de mostrar, escribe el fixture del simulador en esta ruta")
	flag.Parse()

	c := padel.New()

	cal, err := c.Calendar()
	if err != nil {
		fmt.Println("calendario fallo:", err)
		os.Exit(1)
	}
	fmt.Printf("calendario: %d torneos\n", len(cal))

	snap, err := c.Feed(6)
	if err != nil {
		fmt.Println("feed fallo:", err)
		os.Exit(1)
	}

	if snap.Live != nil {
		fmt.Printf("\nEN JUEGO  [%s] %s\n          %s, %s  ·  %s  ·  dia %d de %d\n",
			snap.Live.Cat, snap.Live.Name, snap.Live.City, snap.Live.Country,
			snap.Live.Rango, snap.Day, snap.Days)
	} else {
		fmt.Println("\nEN JUEGO  (ninguno de las categorias elegidas)")
	}

	fmt.Printf("\nPARTIDOS DEL DIA (%d)\n", len(snap.Matches))
	for i, m := range snap.Matches {
		if i >= 8 {
			fmt.Printf("  ... y %d mas\n", len(snap.Matches)-8)
			break
		}
		fmt.Printf("  %-16s %-14s %-8s %s\n", m.Time, m.Court, m.Round+" "+m.Gender, m.State)
		fmt.Printf("      %s / %s  %s  [%s]\n", m.A1, m.A2, m.SeedA, m.ScoreA)
		fmt.Printf("      %s / %s  %s  [%s]\n", m.B1, m.B2, m.SeedB, m.ScoreB)
	}

	fmt.Printf("\nPROXIMOS (%d)\n", len(snap.Next))
	for _, t := range snap.Next {
		fmt.Printf("  [%-8s] %-34s en %2d d  %-12s %s, %s\n",
			t.Cat, t.Name, t.Faltan, t.Rango, t.City, t.Country)
	}

	b, _ := json.Marshal(snap)
	fmt.Printf("\ntamano del JSON que viaja al aparato: %d bytes\n", len(b))

	if *fixture != "" {
		if err := escribirFixture(*fixture, snap); err != nil {
			fmt.Println("no pude escribir el fixture:", err)
			os.Exit(1)
		}
		fmt.Println("fixture escrito en", *fixture)
	}
}
