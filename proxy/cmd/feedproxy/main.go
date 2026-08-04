package main

import (
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/api"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/img"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/rss"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/sorsa"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/trends"
)

const poolTTL = 10 * time.Minute

const trendsLimit = 5

func buildSources() []feed.Source {
	var sources []feed.Source
	key := os.Getenv("SORSA_KEY")

	// En X la mayoria de los tweets son links pelados, sin imagen. En una
	// pantalla que muestra foto + titular esos items caen al avatar del autor
	// y se ven todos iguales, asi que por defecto se descartan.
	// SORSA_MEDIA_ONLY=0 los deja pasar.
	mediaOnly := os.Getenv("SORSA_MEDIA_ONLY") != "0"

	if list := os.Getenv("SORSA_LIST_ID"); key != "" && list != "" {
		sources = append(sources, &sorsa.Source{
			APIKey: key, ListID: list, MediaOnly: mediaOnly,
		})
		log.Printf("fuente: Lista de X (%s)", list)
	}

	// SORSA_QUERY reemplaza a la Lista y no necesita que exista ninguna: no hay
	// forma de descubrir una Lista publica de X, asi que "from:a OR from:b"
	// resuelve lo mismo y se edita aca sin tocar codigo. SORSA_ORDER acepta
	// "latest" (default) o "popular"; con latest, la cuenta que mas postea se
	// lleva la mayoria de los slots.
	if q := os.Getenv("SORSA_QUERY"); key != "" && q != "" {
		sources = append(sources, &sorsa.SearchSource{
			APIKey:    key,
			Query:     q,
			Order:     os.Getenv("SORSA_ORDER"),
			MediaOnly: mediaOnly,
		})
		log.Printf("fuente: busqueda en X (%s)", q)
	}

	if key != "" && os.Getenv("SORSA_LIST_ID") == "" && os.Getenv("SORSA_QUERY") == "" {
		log.Print("SORSA_LIST_ID y SORSA_QUERY sin definir; sigo sin contenido de X")
	}

	// TRENDS_WOEID acepta "23424747|Argentina,1|Mundo". No necesita list_id.
	if key != "" {
		for _, spec := range strings.Split(os.Getenv("TRENDS_WOEID"), ",") {
			spec = strings.TrimSpace(spec)
			if spec == "" {
				continue
			}
			raw, region := spec, "Tendencias"
			if i := strings.LastIndex(spec, "|"); i > 0 {
				raw, region = spec[:i], spec[i+1:]
			}
			woeid, err := strconv.Atoi(raw)
			if err != nil {
				log.Printf("TRENDS_WOEID: %q no es un numero, lo salteo", raw)
				continue
			}
			sources = append(sources, &trends.Source{
				APIKey: key, Woeid: woeid, Region: region, Limit: trendsLimit,
			})
			log.Printf("fuente: tendencias de %s (woeid %d)", region, woeid)
		}
	}

	for _, spec := range strings.Split(os.Getenv("RSS_FEEDS"), ",") {
		spec = strings.TrimSpace(spec)
		if spec == "" {
			continue
		}
		url, label := spec, "rss"
		if i := strings.LastIndex(spec, "|"); i > 0 {
			url, label = spec[:i], spec[i+1:]
		}
		sources = append(sources, &rss.Source{URL: url, Label: label})
		log.Printf("fuente: RSS %s", label)
	}

	return sources
}

func main() {
	addr := os.Getenv("FEED_ADDR")
	if addr == "" {
		addr = "127.0.0.1:9110"
	}

	sources := buildSources()
	if len(sources) == 0 {
		log.Fatal("no hay ninguna fuente configurada: definir RSS_FEEDS, o SORSA_KEY junto con SORSA_QUERY o SORSA_LIST_ID")
	}

	h := api.NewWithImages(mixer.New(poolTTL, sources...), img.NewCache())

	token := os.Getenv("FEED_TOKEN")
	if token == "" {
		log.Print("AVISO: FEED_TOKEN vacio, el feed queda abierto a quien lo pida")
	} else {
		log.Print("token requerido en Authorization: Bearer")
	}
	// 1 pedido cada 2 s con picos de 20: el aparato pide un feed cada 10 min y
	// una imagen cada 17 s, asi que le sobra muy holgado.
	h.SetGuard(api.NewGuard(token, 0.5, 20))

	// FIRMWARE_DIR es el interruptor del OTA: sin la variable los endpoints
	// dan 404 y el aparato solo se actualiza por USB. La carpeta lleva
	// firmware.bin y version.txt, que se suben por scp igual que el proxy.
	if dir := os.Getenv("FIRMWARE_DIR"); dir != "" {
		h.SetFirmware(&api.Firmware{Dir: dir})
		log.Printf("OTA habilitado, sirvo firmware desde %s", dir)
	}

	srv := &http.Server{
		Addr:              addr,
		Handler:           h,
		ReadTimeout:       10 * time.Second,
		ReadHeaderTimeout: 5 * time.Second,
		WriteTimeout:      20 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    8 << 10,
	}
	log.Printf("escuchando en %s", addr)
	log.Fatal(srv.ListenAndServe())
}

