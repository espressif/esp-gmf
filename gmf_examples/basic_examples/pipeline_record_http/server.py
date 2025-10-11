import os, datetime, sys
import wave
import argparse
import socket

if sys.version_info.major == 3:
    # Python3
    from urllib import parse
    from http.server import HTTPServer
    from http.server import BaseHTTPRequestHandler
else:
    # Python2
    import urlparse
    from BaseHTTPServer import HTTPServer
    from BaseHTTPServer import BaseHTTPRequestHandler

PORT = 8000

class Handler(BaseHTTPRequestHandler):
    def _set_headers(self, length):
        self.send_response(200)
        if length > 0:
            self.send_header('Content-length', str(length))
        self.end_headers()

    def _get_chunk_size(self):
        data = self.rfile.read(2)
        while data[-2:] != b'\r\n':
            data += self.rfile.read(1)
        return int(data[:-2], 16)

    def _get_chunk_data(self, chunk_size):
        data = self.rfile.read(chunk_size)
        self.rfile.read(2)
        return data

    def _write_wav(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.wav', time, rates, bits, ch)

        wavfile = wave.open(filename, 'wb')
        wavfile.setparams((ch, int(bits/8), rates, 0, 'NONE', 'NONE'))
        wavfile.writeframesraw(bytearray(data))
        wavfile.close()
        return filename

    def _write_aac(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.aac', time, rates, bits, ch)

        with open(filename, 'wb') as aacfile:
            aacfile.write(bytearray(data))
        return filename

    def _write_g711a(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.g711a', time, rates, bits, ch)

        with open(filename, 'wb') as g711afile:
            g711afile.write(bytearray(data))
        return filename

    def _write_g711u(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.g711u', time, rates, bits, ch)

        with open(filename, 'wb') as g711ufile:
            g711ufile.write(bytearray(data))
        return filename

    def _write_amrnb(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.amr', time, rates, bits, ch)

        with open(filename, 'wb') as amrnbfile:
            amrnbfile.write(b'#!AMR\n')
            amrnbfile.write(bytearray(data))
        return filename

    def _write_amrwb(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.amr', time, rates, bits, ch)

        with open(filename, 'wb') as amrwbfile:
            amrwbfile.write(b'#!AMR-WB\n')
            amrwbfile.write(bytearray(data))
        return filename

    def _write_opus(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.opus', time, rates, bits, ch)

        with open(filename, 'wb') as opusfile:
            opusfile.write(bytearray(data))
        return filename

    def _write_adpcm(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.adpcm', time, rates, bits, ch)

        with open(filename, 'wb') as adpcmfile:
            adpcmfile.write(bytearray(data))
        return filename

    def _write_lc3(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.lc3', time, rates, bits, ch)

        with open(filename, 'wb') as lc3file:
            lc3file.write(bytearray(data))
        return filename

    def _write_sbc(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.sbc', time, rates, bits, ch)

        with open(filename, 'wb') as sbcfile:
            sbcfile.write(bytearray(data))
        return filename

    def do_POST(self):
        if sys.version_info.major == 3:
            urlparts = parse.urlparse(self.path)
        else:
            urlparts = urlparse.urlparse(self.path)
        request_file_path = urlparts.path.strip('/')
        total_bytes = 0
        sample_rates = 0
        bits = 0
        channel = 0
        content_type = ''
        print('Do Post......')
        if (request_file_path == 'upload'
            and self.headers.get('Transfer-Encoding', '').lower() == 'chunked'):
            data = []
            sample_rates = self.headers.get('x-audio-sample-rates', '').lower()
            bits = self.headers.get('x-audio-bits', '').lower()
            channel = self.headers.get('x-audio-channel', '').lower()
            content_type = self.headers.get('Content-Type', '').lower()
            while True:
                chunk_size = self._get_chunk_size()
                total_bytes += chunk_size
                print('Total bytes received: {}'.format(total_bytes))
                sys.stdout.write('\033[F')
                if (chunk_size == 0):
                    break
                else:
                    chunk_data = self._get_chunk_data(chunk_size)
                    data += chunk_data

            if 'audio/pcm' in content_type:
                filename = self._write_wav(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/aac' in content_type:
                filename = self._write_aac(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/g711a' in content_type or 'audio/pcma' in content_type:
                filename = self._write_g711a(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/g711u' in content_type or 'audio/pcmu' in content_type:
                filename = self._write_g711u(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/amr-nb' in content_type or 'audio/amr' in content_type:
                filename = self._write_amrnb(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/amr-wb' in content_type:
                filename = self._write_amrwb(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/opus' in content_type:
                filename = self._write_opus(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/adpcm' in content_type:
                filename = self._write_adpcm(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/lc3' in content_type:
                filename = self._write_lc3(data, int(sample_rates), int(bits), int(channel))
            elif 'audio/sbc' in content_type:
                filename = self._write_sbc(data, int(sample_rates), int(bits), int(channel))
            else:
                self.send_response(400)
                self.send_header('Content-type', 'text/html;charset=utf-8')
                self.end_headers()
                body = 'Unsupported audio format: {}'.format(content_type)
                self.wfile.write(body.encode('utf-8'))
                return

            self.send_response(200)
            self.send_header('Content-type', 'text/html;charset=utf-8')
            self.send_header('Content-Length', str(total_bytes))
            self.end_headers()
            body = 'File {} was written, size {}'.format(filename, total_bytes)
            self.wfile.write(body.encode('utf-8'))

    def do_GET(self):
        print('Do GET')
        self.send_response(200)
        self.send_header('Content-type', 'text/html;charset=utf-8')
        self.end_headers()

def get_host_ip():
    # https://www.cnblogs.com/z-x-y/p/9529930.html
    try:
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        s.connect(('8.8.8.8',80))
        ip=s.getsockname()[0]
    finally:
        s.close()
    return ip

parser = argparse.ArgumentParser(description='HTTP Server save pipeline_record_http example voice data to corresponding file')
parser.add_argument('--ip', '-i', nargs='?', type = str)
parser.add_argument('--port', '-p', nargs='?', type = int)
args = parser.parse_args()
if not args.ip:
    args.ip = get_host_ip()
if not args.port:
    args.port = PORT

httpd = HTTPServer((args.ip, args.port), Handler)

print('Serving HTTP on {} port {}'.format(args.ip, args.port))
httpd.serve_forever()
