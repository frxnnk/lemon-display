package norm

import (
	"regexp"
	"strconv"
	"strings"
	"unicode/utf8"
)

var (
	urlRe       = regexp.MustCompile(`https?://\S+`)
	spaceRe     = regexp.MustCompile(`\s+`)
	numEntityRe = regexp.MustCompile(`&#([xX])?([0-9A-Fa-f]+);`)
)

// translit cubre solo lo que la fuente NO puede dibujar: las comillas
// tipograficas, las rayas y los puntos suspensivos viven arriba de 0x00FF.
//
// Los acentos, la enye y los signos de apertura ya NO se transliteran. La
// fuente del firmware se regenero hasta 0xFF justamente para eso, asi que
// convertir "anos" en "años" es simplemente dejar de romperlo.
var translit = map[rune]string{
	'‘': "'", '’': "'",
	'“': "\"", '”': "\"",
	'–': "-", '—': "-",
	'…': "...",
	// El nbsp entra en el rango de la fuente, pero como espacio de verdad:
	// dibujado es indistinguible y ademas asi lo colapsa spaceRe.
	' ': " ",
}

var entities = map[string]string{
	"&amp;": "&", "&lt;": "<", "&gt;": ">",
	"&quot;": "\"", "&apos;": "'", "&#39;": "'", "&nbsp;": " ",
}

// renderable son los codepoints que la fuente tiene glifo para dibujar. El
// tramo 0x7F-0xA0 queda afuera a proposito: son DEL y los controles C1, que no
// se ven pero ocuparian ancho.
func renderable(r rune) bool {
	return (r >= 0x20 && r <= 0x7E) || (r >= 0xA1 && r <= 0xFF)
}

// decodeNumericEntities convierte &#241; y &#xF1; en el caracter que nombran.
// Antes se descartaban, con el argumento de que "casi siempre son acentos, que
// Clean tirara igual". Ahora los acentos se muestran, asi que descartarlas
// seria tirar justo lo que se quiere leer.
func decodeNumericEntities(s string) string {
	return numEntityRe.ReplaceAllStringFunc(s, func(m string) string {
		sub := numEntityRe.FindStringSubmatch(m)
		base := 10
		if sub[1] != "" {
			base = 16
		}
		n, err := strconv.ParseInt(sub[2], base, 32)
		if err != nil || n <= 0 || n > 0x10FFFF {
			return ""
		}
		return string(rune(n))
	})
}

// StripHTML saca etiquetas y decodifica las entidades. Hace falta porque
// algunos feeds (Xataka) mandan el articulo entero en <description>.
func StripHTML(s string) string {
	var b strings.Builder
	b.Grow(len(s))
	depth := 0
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case '<':
			depth++
		case '>':
			if depth > 0 {
				depth--
				// Espacio al cerrar: sin esto "uno<br/>dos" queda "unodos".
				if depth == 0 {
					b.WriteByte(' ')
				}
			}
		default:
			if depth == 0 {
				b.WriteByte(s[i])
			}
		}
	}

	out := b.String()
	for k, v := range entities {
		out = strings.ReplaceAll(out, k, v)
	}
	out = decodeNumericEntities(out)
	return strings.TrimSpace(spaceRe.ReplaceAllString(out, " "))
}

// Clean deja el texto en codepoints que la fuente pueda dibujar: ASCII mas el
// tramo alto de Latin-1. Lo que queda afuera se translitera si tiene
// equivalente, y si no se cae a un espacio.
//
// La salida es UTF-8, no Latin-1: LovyanGFX decodifica UTF-8 solo, y un
// codepoint como U+00F1 le llega como 0xF1, que cae dentro del rango de la
// fuente.
func Clean(s string) string {
	s = urlRe.ReplaceAllString(s, "[link]")

	var b strings.Builder
	b.Grow(len(s))
	for _, r := range s {
		if rep, ok := translit[r]; ok {
			b.WriteString(rep)
			continue
		}
		if renderable(r) {
			b.WriteRune(r)
			continue
		}
		b.WriteByte(' ')
	}

	return strings.TrimSpace(spaceRe.ReplaceAllString(b.String(), " "))
}

// Truncate recorta en el ultimo espacio antes de max para no partir palabras.
func Truncate(s string, max int) string {
	if len(s) <= max {
		return s
	}
	cut := s[:max]
	if i := strings.LastIndexByte(cut, ' '); i > max/2 {
		cut = cut[:i]
	}
	// max cuenta bytes, y en UTF-8 un acento son dos: cortar ahi puede dejar
	// media secuencia al final, que en pantalla sale como basura. Se retrocede
	// hasta que el ultimo caracter vuelva a ser valido.
	for len(cut) > 0 {
		r, size := utf8.DecodeLastRuneInString(cut)
		if r != utf8.RuneError || size > 1 {
			break
		}
		cut = cut[:len(cut)-1]
	}
	return strings.TrimRight(cut, " ,;:.") + "."
}
