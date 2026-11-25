/*
 * page.h - vestavena stranka serveru
 *
 * Jeden soubor, zadny externi skript, zadny font z CDN, zadny obrazek.
 * V aute neni internet a telefon, ktery se pripoji k pristupovemu bodu
 * desky, nema kudy si cokoliv dotahnout - stranka, ktera se odkazuje
 * ven, by se u nej zasekla na prazdnem ramecku.
 *
 * Stranka sama NEZNA ANI JEDNU VELICINU. Popisky, skupiny i texty
 * legendy si stahne z /meta hned po otevreni (jsou prelozene podle
 * jazyka, ktery ma deska nastaveny), hodnoty pak z /live. Pridani
 * veliciny je tedy jeden radek v tabulce v net/report.c a nic tady.
 *
 * HODNOTY SE NEPOCITAJI V PROHLIZECI. Z /live chodi uz hotovy text i s
 * poctem desetinnych mist a k nemu stav; prohlizec jen vybere barvu.
 * Diky tomu plati ctyri stavy hodnoty na webu uplne stejne jako na
 * displeji a chybejici hodnota nema jak zezelenat na nulu - zadne
 * cislo do prohlizece vubec nedorazi.
 *
 * Obnovuje se DOTAZEM, ne server-sent events; duvod je u NET_LIVE_MS v
 * net/server.h.
 *
 * Kdyz dotaz selze (telefon vypadl ze site, deska se restartovala),
 * stranka to REKNE a cisla ztlumi. Zmrzla cisla, ktera porad vypadaji
 * ziva, jsou ta nejhorsi varianta: clovek se podle nich rozhoduje.
 *
 * Jen ASCII, aby to proslo pravidlem projektu; diakritika chodi az v
 * datech z /meta a hlavicka Content-Type rika UTF-8.
 */
#ifndef NET_PAGE_H
#define NET_PAGE_H

/* Jak casto si stranka rekne o nova cisla. Duvod, proc dotaz a ne
 * server-sent events, je u NET_CLIENTS_MAX v net/server.h.
 *
 * 500 ms je dvakrat za vterinu: rychlejsi uz telefon v ruce nerozezna
 * a stejnou periodu ma i zaznam relace, takze deska ma jen jednu
 * kadenci a ne dve, ktere se muzou rozejit. */
#define NET_PAGE_REFRESH_MS 500

#define NET_PAGE_XSTR(x) #x
#define NET_PAGE_STR(x)  NET_PAGE_XSTR(x)

static const char NET_PAGE_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>canograf</title><style>"
"*{box-sizing:border-box}"
"body{margin:0;padding:10px;background:#0B1016;color:#DCE7F0;"
"font:16px/1.35 system-ui,-apple-system,Helvetica,Arial,sans-serif}"
"h1{font-size:18px;margin:0 0 2px;letter-spacing:.06em}"
"h2{font-size:13px;margin:16px 0 6px;color:#65798B;letter-spacing:.14em;"
"text-transform:uppercase;border-bottom:1px solid #1B2A38;padding-bottom:4px}"
"#top{display:flex;flex-wrap:wrap;gap:8px;align-items:baseline}"
"#bar{font-size:13px;padding:2px 8px;border:1px solid #1B2A38;border-radius:4px}"
"#bar.ok{color:#68937A;border-color:#68937A}"
"#bar.warn{color:#C8432B;border-color:#C8432B}"
"#net,#rows{font-size:13px;color:#65798B}"
".msg{margin:8px 0;padding:8px 10px;border-radius:4px;font-size:14px;"
"background:#0E151D;border-left:4px solid #C8432B;color:#DCE7F0}"
".grid{display:grid;gap:8px;"
"grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}"
".c{background:#0E151D;border:1px solid #1B2A38;border-radius:5px;"
"padding:7px 9px;min-height:62px}"
".n{font-size:11px;color:#65798B;letter-spacing:.08em;"
"text-transform:uppercase;overflow-wrap:anywhere}"
".v{font-size:26px;font-variant-numeric:tabular-nums;margin-top:2px}"
".s0 .v{color:#65798B}"
".s1 .v{color:#F0DFB8}"
".s2{border-left:4px solid #8A7A4F}.s2 .v{color:#8A7A4F}"
".s3{border-left:4px solid #C8432B}.s3 .v{color:#C8432B;font-size:20px}"
"#leg{margin:14px 0 4px;font-size:12px;color:#65798B;display:flex;"
"flex-wrap:wrap;gap:12px}"
"#leg i{font-style:normal;border-left:4px solid;padding-left:5px}"
"#foot{margin:14px 0 4px;font-size:12px;color:#65798B}"
"a.dl{display:inline-block;margin:10px 0;padding:11px 18px;background:#0E151D;"
"border:2px solid #6FA8D0;border-radius:5px;color:#6FA8D0;"
"text-decoration:none;font-size:15px;letter-spacing:.06em}"
"body.off .v{opacity:.35}"
"@media (orientation:landscape){.v{font-size:22px}"
".grid{grid-template-columns:repeat(auto-fit,minmax(125px,1fr))}}"
"</style></head><body>"
"<div id=top><h1 id=t>canograf</h1><span id=bar class=warn>...</span></div>"
"<div id=net></div>"
"<div id=err class=msg style='display:none'></div>"
"<div id=nov class=msg style='display:none'></div>"
"<div id=g></div>"
"<div id=leg></div>"
"<a class=dl id=csv href='/log.csv' download='canograf.csv'>CSV</a>"
"<div id=rows></div>"
"<div id=foot><span id=ro></span><br><span id=note></span></div>"
"<script>\n"
"var M=null,cells=[],fails=0,timer=null;\n"
"function E(i){return document.getElementById(i);}\n"
"function show(i,on){E(i).style.display=on?'block':'none';}\n"
"function get(u,ok){var x=new XMLHttpRequest();x.open('GET',u,true);\n"
" x.timeout=4000;\n"
" x.onload=function(){if(x.status==200){try{ok(JSON.parse(x.responseText));}\n"
"  catch(e){bad();}}else bad();};\n"
" x.onerror=bad;x.ontimeout=bad;x.send();}\n"
"function bad(){if(++fails<2)return;document.body.className='off';\n"
" E('err').textContent=M?M.nosrv:'no answer from the device';show('err',1);}\n"
"function cell(txt){var c=document.createElement('div');c.className='c s0';\n"
" var n=document.createElement('div');n.className='n';n.textContent=txt;\n"
" var v=document.createElement('div');v.className='v';v.textContent='--';\n"
" c.appendChild(n);c.appendChild(v);return{box:c,val:v};}\n"
"function build(m){M=m;document.title=m.title;E('t').textContent=m.title;\n"
" E('ro').textContent=m.ro;E('note').textContent=m.note;\n"
" E('csv').textContent=m.csv;\n"
" var root=E('g');\n"
" for(var g=0;g<m.grp.length;g++){var any=0,d=document.createElement('div');\n"
"  d.className='grid';\n"
"  for(var i=0;i<m.ch.length;i++){if(m.ch[i].g!=g)continue;\n"
"   cells[i]=cell(m.ch[i].n);d.appendChild(cells[i].box);any=1;}\n"
"  if(!any)continue;\n"
"  var h=document.createElement('h2');h.textContent=m.grp[g];\n"
"  root.appendChild(h);root.appendChild(d);}\n"
" var lg=E('leg');\n"
" for(var k=0;k<m.leg.length;k++){var s=document.createElement('i');\n"
"  s.textContent=m.leg[k];\n"
"  s.style.borderColor=['#65798B','#F0DFB8','#8A7A4F','#C8432B'][k];\n"
"  lg.appendChild(s);}\n"
" tick();timer=setInterval(tick," NET_PAGE_STR(NET_PAGE_REFRESH_MS) ");}\n"
"function tick(){get('/live',function(d){fails=0;document.body.className='';\n"
" show('err',0);\n"
" var b=E('bar');b.textContent=d.link;b.className=d.nov?'bar warn':'bar ok';\n"
" E('nov').textContent=M.nolink;show('nov',d.nov);\n"
" var n=d.net,t=(n.ap?'AP ':'')+n.ssid;\n"
" if(n.ip)t+='   '+n.ip;\n"
" if(n.ap&&n.cl>=0)t+='   '+n.cl;\n"
" E('net').textContent=t;\n"
" E('rows').textContent=d.log?(d.rows+' '+M.rows+\n"
"  (d.drop?'   (+'+d.drop+' '+M.drop+')':'')):M.nolog;\n"
" for(var i=0;i<d.v.length;i++){var c=cells[i];if(!c)continue;\n"
"  c.val.textContent=d.v[i].t+(d.v[i].a?'  ('+d.v[i].a+'s)':'');\n"
"  c.box.className='c s'+d.v[i].s;}});}\n"
"get('/meta',build);\n"
"</script></body></html>";

#endif /* NET_PAGE_H */
