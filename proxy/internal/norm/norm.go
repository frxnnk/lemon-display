package norm

import (
	"regexp"
	"strings"
)

var (
	urlRe   = regexp.MustCompile(`https?://\S+`)
	spaceRe = regexp.MustCompile(`\s+`)
)

var translit = map[rune]string{
	'á': "a", 'é': "e", 'í': "i", 'ó': "o", 'ú': "u", 'ü': "u", 'ñ': "n",
	'Á': "A", 'É': "E", 'Í': "I", 'Ó': "O", 'Ú': "U", 'Ü': "U", 'Ñ': "N",
	'à': "a", 'è': "e", 'ì': "i", 'ò': "o", 'ù': "u",
	'À': "A", 'È': "E", 'Ì': "I", 'Ò': "O", 'Ù': "U",
	'â': "a", 'ê': "e", 'î': "i", 'ô': "o", 'û': "u",
	'Â': "A", 'Ê': "E", 'Î': "I", 'Ô': "O", 'Û': "U",
	'ã': "a", 'õ': "o", 'Ã': "A", 'Õ': "O",
	'ç': "c", 'Ç': "C",
	'‘': "'", '’': "'",
	'“': "\"", '”': "\"",
	'–': "-", '—': "-",
	'…': "...",
	' ': " ",
	'¿': "", '¡': "",
}

var entities = map[string]string{
	"&amp;": "&", "&lt;": "<", "&gt;": ">",
	"&quot;": "\"", "&apos;": "'", "&#39;": "'", "&nbsp;": " ",
}

// StripHTML saca etiquetas y decodifica las entidades comunes. Hace falta
// porque algunos feeds (Xataka) mandan el articulo entero en <description>.
// Las entidades numericas se descartan: casi siempre son acentos, que Clean
// tirara igual.
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
	// Cualquier entidad numerica remanente se borra.
	for {
		i := strings.Index(out, "&#")
		if i < 0 {
			break
		}
		j := strings.IndexByte(out[i:], ';')
		if j < 0 || j > 8 {
			break
		}
		out = out[:i] + out[i+j+1:]
	}
	return strings.TrimSpace(spaceRe.ReplaceAllString(out, " "))
}

// Clean deja el texto en ASCII puro: la fuente embebida del firmware descarta
// cualquier codepoint fuera de ASCII, asi que lo que no se translitera se cae.
func Clean(s string) string {
	s = urlRe.ReplaceAllString(s, "[link]")

	var b strings.Builder
	b.Grow(len(s))
	for _, r := range s {
		if rep, ok := translit[r]; ok {
			b.WriteString(rep)
			continue
		}
		if r < 128 {
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
	return strings.TrimRight(cut, " ,;:.") + "."
}
