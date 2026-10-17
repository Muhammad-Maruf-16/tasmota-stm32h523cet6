#ifndef HTML_LANDING_H
#define HTML_LANDING_H

// =============================================================================
// LANDING PAGE — disimpan di flash internal STM32
// Ditampilkan jika HTML di SPI Flash kosong / corrupt
// Fungsi: upload file HTML gabungan ke SPI Flash
// Ukuran target: < 800 byte
// =============================================================================
static const char HTML_LANDING[] =
"<!DOCTYPE html><html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>STM32 Setup</title>"
"<style>"
"body{font-family:sans-serif;background:#1e1e1e;color:#ddd;text-align:center;padding:30px}"
"h2{color:#1fa3ec}p{color:#888;font-size:13px}"
"input{margin:10px 0}"
"button{padding:10px 24px;background:#1fa3ec;color:#fff;border:none;"
"border-radius:3px;font-size:14px;cursor:pointer}"
"button:hover{background:#1b8fd4}"
"#st{margin-top:14px;font-size:13px;color:#aaa}"
"</style></head><body>"
"<h2>&#9881; STM32 W5500</h2>"
"<p>HTML belum tersedia di flash.<br>Upload file HTML untuk mengaktifkan dashboard.</p>"
"<input type='file' id='f' accept='.html'><br>"
"<button onclick='upload()'>&#8679; Upload HTML</button>"
"<div id='st'>Pilih file .html</div>"
"<script>"
"function crc32(b){"
"let pad=(4-(b.length%4))%4,buf=new Uint8Array(b.length+pad);"
"buf.set(b);for(let z=0;z<pad;z++)buf[b.length+z]=0xFF;"
"let v=new DataView(buf.buffer),"
"c=0xFFFFFFFF,p=0x04C11DB7,w=buf.length/4;"
"for(let i=0;i<w;i++){c^=v.getUint32(i*4,true);"
"for(let k=0;k<32;k++)c=(c&0x80000000)?((c<<1)^p)>>>0:(c<<1)>>>0;}"
"return'0x'+(c>>>0).toString(16).toUpperCase();}"
"async function upload(){"
"let f=document.getElementById('f').files[0];"
"if(!f)return;"
"let a=new Uint8Array(await f.arrayBuffer());"
"let crc=crc32(a);"
"document.getElementById('st').innerText='Mengunggah... CRC:'+crc;"
"try{"
"let r=await fetch('/html-upload?sz='+a.length+'&crc='+crc,"
"{method:'POST',body:a});"
"let t=await r.text();"
"document.getElementById('st').innerText=t;"
"if(r.ok)setTimeout(()=>location.href='/',2000);"
"}catch(e){document.getElementById('st').innerText='Gagal: '+e;}}"
"</script></body></html>";

#endif /* HTML_LANDING_H */
