// Package padel arma la vista de padel profesional que consume el aparato:
// que torneo se esta jugando, que partidos vienen hoy y cuales son los proximos
// torneos del circuito.
//
// Las dos fuentes son publicas y estan renderizadas del lado del servidor, asi
// que no hace falta clave ni ejecutar JavaScript:
//
//	padelfip.com/es/calendario/?events-year=YYYY   calendario completo del ano
//	padelfip.com/es/eventos/<slug>/                 pagina del torneo
//	widget.matchscorerlive.com/screen/oopbyday/...  orden de juego del dia
//
// La cadena entre las tres la descubre este paquete: la pagina del torneo trae
// el endpoint get-oop-data.php, y ese endpoint devuelve el dia que se esta
// jugando y la URL exacta del widget.
//
// Igual que con el resto del proxy, todo lo fragil vive aca: el firmware recibe
// texto ya limpio, en castellano y del largo que entra en pantalla.
package padel

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"
)

const (
	calendarURL = "https://www.padelfip.com/es/calendario/?events-year=%d"
	oopDataURL  = "https://www.padelfip.com/wp-content/themes/padelfiptheme/" +
		"template-parts/event/endpoint/get-oop-data.php?year=%d&id=%s&day=%d&totalday=%d&widget=oopbyday"

	// El calendario del ano pesa ~2 MB y cambia de mes en mes: pedirlo seguido
	// seria maltratar a un tercero para no enterarse de nada.
	calendarTTL = 6 * time.Hour
	// El orden de juego si cambia dentro del dia, pero un partido dura mas de
	// una hora: cinco minutos alcanza y sobra.
	oopTTL = 5 * time.Minute

	// El calendario entero no entra en un limite chico, y truncarlo perderia
	// torneos enteros. Es el mismo problema que el limite del RSS.
	maxCalendarBytes = 8 << 20
	maxPageBytes     = 4 << 20
)

// Tournament es un torneo del calendario.
type Tournament struct {
	Slug    string    `json:"-"`
	Name    string    `json:"nombre"`
	Cat     string    `json:"cat"`
	City    string    `json:"ciudad"`
	Country string    `json:"pais"`
	Start   time.Time `json:"-"`
	End     time.Time `json:"-"`

	// Fechas ya formateadas para pantalla: el firmware no hace aritmetica de
	// calendario ni conoce los meses en castellano.
	Rango  string `json:"rango"`
	Faltan int    `json:"faltan"` // dias hasta el inicio; 0 si ya arranco
}

// Match es un partido del orden de juego del dia.
type Match struct {
	Court  string `json:"cancha"`
	Time   string `json:"hora"` // "10:00", "a seguir", "no antes de 15:30"
	Round  string `json:"fase"` // "R32", "Octavos", "Cuartos", "Semis", "FINAL"
	Gender string `json:"gen"`  // "M" | "F"
	A1     string `json:"a1"`
	A2     string `json:"a2"`
	B1     string `json:"b1"`
	B2     string `json:"b2"`
	SeedA  string `json:"sa,omitempty"`
	SeedB  string `json:"sb,omitempty"`
	ScoreA string `json:"ra,omitempty"` // "6 4 6"
	ScoreB string `json:"rb,omitempty"`
	State  string `json:"est"` // "" (por jugar) | "jugando" | "listo"
}

// Snapshot es lo que se sirve en /v1/padel.
type Snapshot struct {
	// Torneo en juego. nil cuando no hay ninguno de las categorias elegidas.
	Live    *Tournament  `json:"torneo,omitempty"`
	Day     int          `json:"dia,omitempty"`
	Days    int          `json:"dias,omitempty"`
	Matches []Match      `json:"partidos"`
	Next    []Tournament `json:"proximos"`
}

// Categorias del circuito, de mayor a menor jerarquia. El slug es la clase CSS
// que padelfip.com le pone al <article> de cada torneo.
var categorias = []struct {
	slug  string
	label string
}{
	{"fip-ppt-major", "MAJOR"},
	{"fip-pp-master-finals", "MASTER"},
	{"fip-ppt-p1", "P1"},
	{"fip-pp-p2", "P2"},
	{"fip-championship", "FIP CHAMP"},
	{"fip-tour-platinum", "PLATINUM"},
	{"fip-tour-gold", "GOLD"},
	{"fip-tour-silver", "SILVER"},
	{"fip-tour-bronze", "BRONZE"},
}

// DefaultCats son "las mejores ligas": Premier Padel entero mas el escalon alto
// del Cupra FIP Tour. Queda afuera lo regional y lo juvenil (silver, bronze,
// promises), que son ~380 de los 449 torneos del ano y taparian a los demas.
var DefaultCats = []string{"MAJOR", "MASTER", "P1", "P2", "PLATINUM", "GOLD"}

func labelDeSlug(clase string) string {
	for _, c := range categorias {
		if strings.Contains(clase, c.slug) {
			return c.label
		}
	}
	return ""
}

// rank ordena por jerarquia: con dos torneos en juego a la vez gana el mas
// importante, que es el que el usuario quiere ver.
func rank(label string) int {
	for i, c := range categorias {
		if c.label == label {
			return i
		}
	}
	return len(categorias)
}

// Client baja y cachea. Now es inyectable para que los tests no dependan del
// dia en que corren.
type Client struct {
	HTTP *http.Client
	Now  func() time.Time
	Cats []string

	// BaseCalendar y BaseOOPData permiten apuntar a un servidor de prueba.
	BaseCalendar string
	BaseOOPData  string

	mu     sync.Mutex
	cal    []Tournament
	calAt  time.Time
	snap   *Snapshot
	snapAt time.Time
	oopIDs map[string]oopRef // slug -> referencia al widget
}

type oopRef struct {
	year, day, totalDay int
	id                  string
}

func New() *Client {
	return &Client{
		HTTP: &http.Client{Timeout: 25 * time.Second},
		Now:  time.Now,
		Cats: DefaultCats,
	}
}

func (c *Client) now() time.Time {
	if c.Now != nil {
		return c.Now()
	}
	return time.Now()
}

func (c *Client) cats() []string {
	if len(c.Cats) > 0 {
		return c.Cats
	}
	return DefaultCats
}

func (c *Client) quiere(cat string) bool {
	for _, x := range c.cats() {
		if x == cat {
			return true
		}
	}
	return false
}

// get baja una URL con un limite de cuerpo. El limite no es paranoia: el
// calendario del ano pesa 2 MB y una respuesta rota podria ser mucho mayor.
func (c *Client) get(url string, max int64) ([]byte, error) {
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	// padelfip.com responde 403 a los clientes sin User-Agent de navegador.
	req.Header.Set("User-Agent", "Mozilla/5.0 (compatible; ferced-display/1.0)")
	req.Header.Set("Accept-Language", "es-ES,es;q=0.9,en;q=0.8")

	cl := c.HTTP
	if cl == nil {
		cl = http.DefaultClient
	}
	resp, err := cl.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s: HTTP %d", url, resp.StatusCode)
	}
	return io.ReadAll(io.LimitReader(resp.Body, max))
}

// Calendar devuelve el calendario del ano en curso, cacheado.
func (c *Client) Calendar() ([]Tournament, error) {
	c.mu.Lock()
	if c.cal != nil && c.now().Sub(c.calAt) < calendarTTL {
		out := c.cal
		c.mu.Unlock()
		return out, nil
	}
	c.mu.Unlock()

	base := c.BaseCalendar
	if base == "" {
		base = calendarURL
	}
	body, err := c.get(fmt.Sprintf(base, c.now().Year()), maxCalendarBytes)
	if err != nil {
		// Ante fallo se conserva lo anterior: mejor un calendario de hace unas
		// horas que una pantalla vacia.
		c.mu.Lock()
		defer c.mu.Unlock()
		if c.cal != nil {
			return c.cal, nil
		}
		return nil, err
	}

	ts := ParseCalendar(string(body))
	if len(ts) == 0 {
		c.mu.Lock()
		defer c.mu.Unlock()
		if c.cal != nil {
			return c.cal, nil
		}
		return nil, fmt.Errorf("el calendario no trajo ningun torneo")
	}

	c.mu.Lock()
	c.cal, c.calAt = ts, c.now()
	c.mu.Unlock()
	return ts, nil
}

// Feed arma el snapshot: torneo en juego con su orden de juego, y los proximos.
func (c *Client) Feed(nProximos int) (*Snapshot, error) {
	c.mu.Lock()
	if c.snap != nil && c.now().Sub(c.snapAt) < oopTTL {
		s := c.snap
		c.mu.Unlock()
		return s, nil
	}
	c.mu.Unlock()

	cal, err := c.Calendar()
	if err != nil {
		return nil, err
	}

	hoy := c.now()
	dia := time.Date(hoy.Year(), hoy.Month(), hoy.Day(), 0, 0, 0, 0, hoy.Location())

	var live *Tournament
	var next []Tournament
	for i := range cal {
		t := cal[i]
		if !c.quiere(t.Cat) {
			continue
		}
		switch {
		case !t.Start.After(dia) && !t.End.Before(dia):
			// En juego. Con dos a la vez gana el de mayor jerarquia.
			if live == nil || rank(t.Cat) < rank(live.Cat) {
				cp := t
				live = &cp
			}
		case t.Start.After(dia):
			t.Faltan = int(t.Start.Sub(dia).Hours() / 24)
			next = append(next, t)
		}
	}

	if len(next) > nProximos {
		next = next[:nProximos]
	}

	snap := &Snapshot{Live: live, Next: next, Matches: []Match{}}

	if live != nil {
		ref, err := c.oopRefFor(live.Slug)
		if err == nil {
			snap.Day, snap.Days = ref.day, ref.totalDay
			if ms, err := c.orderOfPlay(ref); err == nil {
				snap.Matches = ms
			}
		}
	}

	c.mu.Lock()
	c.snap, c.snapAt = snap, c.now()
	c.mu.Unlock()
	return snap, nil
}

// oopRefFor saca de la pagina del torneo el id que usa el widget y en que dia
// va el torneo. El id no se puede deducir del slug: hay que leerlo.
func (c *Client) oopRefFor(slug string) (oopRef, error) {
	c.mu.Lock()
	ref, ok := c.oopIDs[slug]
	c.mu.Unlock()

	if !ok {
		body, err := c.get("https://www.padelfip.com/es/eventos/"+slug+"/", maxPageBytes)
		if err != nil {
			return oopRef{}, err
		}
		ref, err = ParseOOPRef(string(body))
		if err != nil {
			return oopRef{}, err
		}
		c.mu.Lock()
		if c.oopIDs == nil {
			c.oopIDs = map[string]oopRef{}
		}
		c.oopIDs[slug] = ref
		c.mu.Unlock()
	}

	// El endpoint php dice que dia se esta jugando de verdad: el que figura en
	// el HTML es el del momento en que se genero la pagina, que puede ser de
	// ayer si esta cacheada.
	base := c.BaseOOPData
	if base == "" {
		base = oopDataURL
	}
	body, err := c.get(fmt.Sprintf(base, ref.year, ref.id, ref.day, ref.totalDay), 64<<10)
	if err != nil {
		return ref, nil // el dia del HTML es una aproximacion valida
	}
	var r struct {
		Success bool `json:"success"`
		UsedDay int  `json:"usedDay"`
	}
	if json.Unmarshal(body, &r) == nil && r.Success && r.UsedDay > 0 {
		ref.day = r.UsedDay
	}
	return ref, nil
}

func (c *Client) orderOfPlay(ref oopRef) ([]Match, error) {
	url := fmt.Sprintf("https://widget.matchscorerlive.com/screen/oopbyday/FIP-%d-%s/%d?t=tol&culture=en",
		ref.year, ref.id, ref.day)
	body, err := c.get(url, maxPageBytes)
	if err != nil {
		return nil, err
	}
	return ParseOOP(string(body)), nil
}
