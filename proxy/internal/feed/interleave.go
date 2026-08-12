package feed

// Interleave reparte los items en round-robin por origen, conservando el orden
// relativo dentro de cada uno.
//
// El orden cronologico puro agrupa las tendencias al frente: no traen fecha
// propia, comparten el instante de la consulta y quedan todas juntas. En una
// pantalla que rota cada 17 s eso son varios minutos del mismo tipo de
// contenido seguido. Intercalar da variedad sin romper el "hace X min", porque
// dentro de cada origen se sigue respetando la fecha.
//
// El orden de los grupos sigue la primera aparicion, que a su vez sigue el
// orden en que se configuraron las fuentes.
func Interleave(items []Item) []Item {
	if len(items) < 2 {
		return append([]Item(nil), items...)
	}

	var order []string
	groups := make(map[string][]Item)
	for _, it := range items {
		k := it.Src
		if k == "" {
			k = string(it.From)
		}
		if _, ok := groups[k]; !ok {
			order = append(order, k)
		}
		groups[k] = append(groups[k], it)
	}

	if len(order) == 1 {
		return append([]Item(nil), items...)
	}

	out := make([]Item, 0, len(items))
	for len(out) < len(items) {
		for _, o := range order {
			g := groups[o]
			if len(g) == 0 {
				continue
			}
			out = append(out, g[0])
			groups[o] = g[1:]
		}
	}
	return out
}
