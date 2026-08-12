"""Extrae la web embebida y le conecta una API simulada para inspección visual."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/todo_page.h"
DEFAULT_OUTPUT = ROOT / "output/todo-preview.html"


MOCK = r"""<script>
var mockNow=Math.floor(Date.now()/1000),mockData={v:2,rev:7,max:24,items:[
 {id:1,title:'Preparar presentación completa para la reunión del viernes con el equipo',description:'Revisar las métricas del trimestre.\nAgregar las capturas nuevas y cerrar con próximos pasos.',done:false,collapsed:false,due:mockNow+7200,reminder:mockNow+3600,reminderFired:false,subtasks:[{id:11,title:'Actualizar métricas',done:true},{id:12,title:'Elegir capturas',done:false},{id:13,title:'Ensayar cierre',done:false}]},
 {id:2,title:'Pedir turno para el service del auto',description:'',done:false,collapsed:true,due:mockNow-3600,reminder:0,reminderFired:true,subtasks:[]},
 {id:3,title:'Comprar café y frutas',description:'',done:false,collapsed:true,due:0,reminder:0,reminderFired:false,subtasks:[]},
 {id:4,title:'Enviar comprobantes de agosto',description:'Guardados en la carpeta compartida.',done:true,collapsed:true,due:mockNow-86400,reminder:0,reminderFired:true,subtasks:[]}
]};
function mockFetch(path,options){
 var body=new URLSearchParams(options&&options.body||''),route=String(path),item,id=Number(body.get('id')),taskId=Number(body.get('taskId'));
 if(route.indexOf('/api/tasks/create')===0){id=Math.max.apply(null,mockData.items.map(function(x){return x.id}).concat([0]))+1;mockData.items.push({id:id,title:body.get('title'),description:body.get('description')||'',done:false,collapsed:true,due:Number(body.get('due')||0),reminder:Number(body.get('reminder')||0),reminderFired:false,subtasks:[]});mockData.created=id}
 item=mockData.items.find(function(x){return x.id===(taskId||id)});
 if(route.indexOf('/api/tasks/done')===0&&item)item.done=body.get('done')==='true';
 if(route.indexOf('/api/tasks/collapse')===0&&item)item.collapsed=body.get('collapsed')==='true';
 if(route.indexOf('/api/tasks/delete')===0)mockData.items=mockData.items.filter(function(x){return x.id!==id});
 if(route.indexOf('/api/tasks/update')===0&&item){item.title=body.get('title');item.description=body.get('description')||'';item.due=Number(body.get('due')||0);item.reminder=Number(body.get('reminder')||0)}
 if(route.indexOf('/api/subtasks/create')===0&&item){var sid=Math.floor(Math.random()*100000)+100;item.subtasks.push({id:sid,title:body.get('title'),done:false})}
 if(route.indexOf('/api/subtasks/delete')===0&&item)item.subtasks=item.subtasks.filter(function(x){return x.id!==id});
 if(route.indexOf('/api/subtasks/done')===0&&item){var sub=item.subtasks.find(function(x){return x.id===id});if(sub)sub.done=body.get('done')==='true'}
 mockData.rev++;return Promise.resolve({ok:true,status:200,json:function(){return Promise.resolve(JSON.parse(JSON.stringify(mockData)))}})
}
window.fetch=mockFetch;
</script>"""


def extract_preview(output: pathlib.Path = DEFAULT_OUTPUT) -> pathlib.Path:
    source = HEADER.read_text(encoding="utf-8")
    match = re.search(r'R"HTML\((<!doctype html>.*?</html>)\)HTML";', source, re.S)
    if not match:
        raise RuntimeError("no se encontró la página embebida")
    page = match.group(1).replace("<script>", MOCK + "\n<script>", 1)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(page, encoding="utf-8")
    return output


if __name__ == "__main__":
    destination = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT
    print(extract_preview(destination))
