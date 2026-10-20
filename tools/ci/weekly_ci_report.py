#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0
"""Post one comment listing test cases that failed on landed branches.

Counts GMF ``main`` and ADF ``master`` / ``release/*`` only. Merge request
pipelines are omitted. One GitLab issue note per week; running again in the
same week replaces that note.

Token: ``GITLAB_TOKEN`` or ``~/.config/gitlab/espressif-cn-api.token``.
The token needs the ``api`` scope and is not stored in this repo.
"""

import io
import json
import os
import re
import ssl
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

def host():
    value = (os.environ.get('CI_SERVER_URL') or os.environ.get('GITLAB_HTTP_SERVER') or '').strip().rstrip('/')
    if value:
        return value
    raise SystemExit('Set CI_SERVER_URL or GITLAB_HTTP_SERVER')


HOST = host()
HOME_PROJECT = 'adf/multimedia/esp-gmf'
DEFAULT_ISSUE_IID = int(os.environ.get('WEEKLY_REPORT_ISSUE_IID', '8'))
ISSUE_TITLE = 'Weekly CI Failed Cases'
PROJECTS = (
    ('GMF', 'adf/multimedia/esp-gmf'),
    ('ADF', 'adf/esp-adf-internal'),
)
TZ = timezone(timedelta(hours=8))
ANSI = re.compile(r'\x1b\[[0-9;]*m')
UNITY_FAIL = re.compile(r':([A-Za-z_][\w]*):FAIL:(.*)$')
COMPILER = re.compile(
    r'/builds/\S+?/(?:esp-gmf|esp-adf-internal)/'
    r'(\S+?)/([^/\s]+\.(?:c|cc|cpp|h|S)):(\d+):\d+:\s*(?:fatal\s+)?error:\s*(.*)$'
)
PYTEST_SUMMARY = re.compile(
    r'^(?:ERROR|FAILED)\s+(\S+\.py)::([A-Za-z_][\w\[\]-]*)\s+-\s+(.*)$'
)
EXC_REASON = re.compile(
    r'(?:ValueError|AssertionError|TimeoutError|OSError|RuntimeError):\s*(.*)'
)
CTX = ssl.create_default_context(
    cafile=os.environ.get('GITLAB_CA_BUNDLE') or os.environ.get('CI_SERVER_TLS_CA_FILE') or None
)


def token():
    value = os.environ.get('GITLAB_TOKEN', '').strip()
    if value:
        return value
    path = Path.home() / '.config/gitlab/espressif-cn-api.token'
    if path.is_file():
        return path.read_text().strip()
    raise SystemExit('Set GITLAB_TOKEN or create ~/.config/gitlab/espressif-cn-api.token')


TOKEN = token()


def api(method, path, payload=None):
    url = path if path.startswith('http') else HOST + '/api/v4' + path
    data = None
    headers = {'PRIVATE-TOKEN': TOKEN}
    if payload is not None:
        headers['Content-Type'] = 'application/json'
        data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, context=CTX, timeout=120) as resp:
            body = resp.read()
            parsed = json.loads(body) if body else None
            return parsed, dict(resp.headers)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode('utf-8', 'replace')[:400]
        raise SystemExit(f'GitLab {method} {path} -> {exc.code} {detail}') from exc


def paginate(path):
    page = 1
    rows = []
    while True:
        sep = '&' if '?' in path else '?'
        data, hdr = api('GET', f'{path}{sep}per_page=100&page={page}')
        rows.extend(data or [])
        nxt = hdr.get('X-Next-Page') or ''
        if not nxt:
            return rows
        page = int(nxt)


def enc(path):
    return urllib.parse.quote(path, safe='')


def clean(text):
    return ANSI.sub('', text or '').strip()


def is_landed(ref):
    return ref in ('main', 'master') or ref.startswith('release/')


def is_build_job(job):
    name = (job.get('name') or '').lower()
    stage = (job.get('stage') or '').lower()
    return stage == 'build' or name.startswith(('build_', 'component_test_'))


INFRA_FAILURE_REASONS = {
    'runner_system_failure',
    'stuck_or_timeout_failure',
    'scheduler_failure',
    'data_integrity_failure',
    'runner_unsupported',
    'stale_schedule',
    'archived_failure',
    'unmet_prerequisites',
}

INFRA_TRACE_PATTERNS = (
    'The git server, Gitaly, is not available',
    'fatal: unable to access',
    'fatal: Could not read from remote repository',
    'ERROR: Preparation failed:',
    'Error response from daemon',
    'toomanyrequests',
    'no active runners online',
)


def is_prestart_or_infra_failure(job, text=''):
    reason = (job.get('failure_reason') or '').strip()
    if reason in INFRA_FAILURE_REASONS:
        return f'runner/infra failure ({reason})'
    if text:
        for pat in INFRA_TRACE_PATTERNS:
            if pat.lower() in text.lower():
                return f'network/docker/git error ({pat})'
        if is_build_job(job):
            started_markers = (
                'build_apps.py',
                'esp_bmgr_pre_build.py',
                'idf.py',
                'Executing action: all',
                'Running cmake',
                'Solving dependencies',
                'Compiling ',
            )
            if not any(m in text for m in started_markers):
                return 'build job failed before compilation started'
    return ''


def classify_compile_failure(reason, full_line=''):
    blob = f'{reason}\n{full_line}'.lower()
    if any(
        k in blob
        for k in (
            'version solving failed',
            'solvererror',
            'solving dependencies',
            'componentmanagererror',
            'failed to download component',
            'cannot find component',
            'manifest',
            'idf_component.yml',
        )
    ):
        return 'Dependency Error'
    if any(
        k in blob
        for k in (
            '/esp-idf/components/',
            'implicit declaration of function',
            'too few arguments to function',
            'too many arguments to function',
            'deprecated',
            'idf_target',
        )
    ):
        return 'IDF Compatibility'
    return 'Code / Build Error'


def classify_board_failure(reason, detail=''):
    blob = f'{reason}\n{detail}'.lower()
    if any(
        k in blob
        for k in (
            'esptool',
            'fatalerror',
            'serialexception',
            'serial data stream stopped',
            'failed to write to target ram',
            'no serial data received',
            'failed to connect to esp',
            'wrong chip argument',
            "couldn't auto detect chip",
            'serial stream stopped while verifying flash',
            'failed on setup',
            'flasher_args.json not found',
            'requires chip revision in range',
            'sdmmc_card_init failed',
            'failed to mount filesystem',
            'failed to init sd card',
            'sd card mount failed',
        )
    ):
        return 'Env / Flash Error'
    return 'Test Case Failure'


def enrich_dut_Context(reason, dut_log):
    if not dut_log:
        return reason
    if any(k in dut_log for k in ('sdmmc_card_init failed', 'Failed to mount filesystem', 'Failed to init SD card')):
        if 'SD card mount failed' not in reason:
            return f'{reason} (SD card mount failed)'[:200]
    if 'Unrecoverable host sdio state' in dut_log or 'sd_host_wait_for_event returned 0x107' in dut_log:
        if 'SDMMC/SDIO timeout' not in reason:
            return f'{reason} (SDMMC/SDIO timeout)'[:200]
    panic = re.search(r"Guru Meditation Error: Core\s+\d+\s+panic'ed \([^)]+\)", dut_log)
    if panic and 'Guru Meditation' not in reason:
        return f'{reason} ({panic.group(0)})'[:200]
    if 'abort() was called' in dut_log and 'abort()' not in reason:
        return f'{reason} (abort() called)'[:200]
    return reason



def case_reason(case):
    lines = [clean(ln) for ln in (case.get('system_output') or '').splitlines()]
    lines = [ln for ln in lines if ln]
    for key in ('AssertionError', 'TIMEOUT', 'FAIL:', 'Error'):
        for ln in lines:
            if key in ln and 'site-packages' not in ln:
                return ln[:180]
    return (lines[-1] if lines else '')[:180]


def job_text(project, job_id, kind):
    url = f'{HOST}/api/v4/projects/{enc(project)}/jobs/{job_id}/{kind}'
    req = urllib.request.Request(url, headers={'PRIVATE-TOKEN': TOKEN})
    with urllib.request.urlopen(req, context=CTX, timeout=180) as resp:
        return resp.read()


def short_exc(msg):
    text = re.sub(r'\s+', ' ', msg or '').strip()
    missed = re.search(r'Not found "([^"]+)"', text)
    if missed:
        wanted = missed.group(1).replace(r'\[', '[').replace(r'\]', ']')
        return f'Not found "{wanted}"'[:200]
    match = EXC_REASON.search(text)
    if match:
        text = match.group(1).strip()
    if text.count('"') % 2 == 1:
        text = text[:-1].rstrip()
    return text[:200]


def board_reason(message, detail=''):
    blob = f'{message}\n{detail}'
    if 'StopIteration' in blob and ('read_spiflash_sfdp' in blob or '_slip_reader' in blob):
        return 'Serial stream stopped while verifying flash'
    return short_exc(message)


def case_label(file_path, node):
    folder = Path(file_path).parent.name
    if folder:
        return f'{folder}/{node}'
    return node


def unity_message(msg):
    msg = re.sub(r'^Function(?:\s*\[[^\]]*\])+\.\s*', '', msg.strip())
    msg = msg.split('======', 1)[0]
    return re.sub(r'\s+', ' ', msg).strip()[:200]


def unity_fails(text):
    found = []
    seen = set()
    for ln in text.splitlines():
        match = UNITY_FAIL.search(clean(ln))
        if not match:
            continue
        name = match.group(1)
        msg = unity_message(match.group(2))
        if (name, msg) in seen:
            continue
        seen.add((name, msg))
        found.append((name, msg))
    return found


def open_artifacts(project, job_id):
    try:
        blob = job_text(project, job_id, 'artifacts')
        return zipfile.ZipFile(io.BytesIO(blob))
    except (urllib.error.HTTPError, zipfile.BadZipFile, OSError):
        return None


def dut_text(archive, pytest_name):
    chunks = []
    tail = (pytest_name or '').split('.')[-1]
    for name in archive.namelist():
        if name.endswith('dut.txt') and pytest_name and (pytest_name in name or (tail and tail in name)):
            chunks.append(archive.read(name).decode('utf-8', 'replace'))
    return '\n'.join(chunks)


def junit_fails(archive):
    xml_name = next((name for name in archive.namelist() if name.endswith('XUNIT_RESULT.xml')), None)
    if not xml_name:
        return []
    try:
        root = ET.fromstring(archive.read(xml_name))
    except ET.ParseError:
        return []
    found = []
    seen = set()
    for case in root.iter('testcase'):
        node = case.find('error')
        if node is None:
            node = case.find('failure')
        if node is None:
            continue
        message = node.get('message') or ''
        pytest_name = case.get('name') or ''
        if 'Unity test failed' in message:
            for uname, umsg in unity_fails(dut_text(archive, pytest_name)):
                key = (uname, umsg)
                if key in seen:
                    continue
                seen.add(key)
                found.append({'name': uname, 'reason': umsg, 'kind': 'target_test', 'category': 'Test Case Failure'})
            continue
        parts = pytest_name.split('.')
        test = parts[-1] if parts else pytest_name
        chip = parts[0] if len(parts) >= 3 and parts[0].startswith('esp') else ''
        label = case_label(case.get('file') or '', f'{test}[{chip}]' if chip else test)
        dut_log = dut_text(archive, pytest_name)
        reason = enrich_dut_Context(board_reason(message, node.text or ''), dut_log)
        category = classify_board_failure(reason, f"{message}\n{node.text or ''}\n{dut_log[:2000]}")
        key = (label, reason)
        if key in seen:
            continue
        seen.add(key)
        found.append({'name': label, 'reason': reason, 'kind': 'target_test', 'category': category})
    return found


def pytest_summary_fails(text):
    found = []
    seen = set()
    for ln in text.splitlines():
        match = PYTEST_SUMMARY.match(clean(ln))
        if not match or 'Unity test failed' in match.group(3):
            continue
        name = case_label(match.group(1), match.group(2))
        reason = board_reason(match.group(3), text if 'StopIteration' in match.group(3) else '')
        category = classify_board_failure(reason, match.group(3))
        if (name, reason) in seen:
            continue
        seen.add((name, reason))
        found.append({'name': name, 'reason': reason, 'kind': 'target_test', 'category': category})
    return found


def _normalize_compile_target(rel_dir: str) -> str:
    parts = [p for p in rel_dir.strip('/').split('/') if p]
    if 'test_apps' in parts:
        idx = parts.index('test_apps')
        return '/'.join(parts[: idx + 1])
    if 'examples' in parts:
        idx = parts.index('examples')
        end = min(len(parts), idx + 2)
        return '/'.join(parts[:end])
    if parts and parts[0] in ('gmf_examples', 'adf_examples'):
        return '/'.join(parts[: min(len(parts), 3)])
    if len(parts) >= 2 and parts[0] in ('components', 'packages', 'elements'):
        return '/'.join(parts[:2])
    return '/'.join(parts[:2]) if parts else rel_dir


DEP_ERR_RE = re.compile(
    r'(?:Version solving failed|SolverError|ComponentManagerError|'
    r'Solving dependencies failed|Failed to download component|Cannot find component)[^\n]*',
    re.IGNORECASE,
)


def compile_failures(text, default_app=''):
    found = []
    seen = set()
    for raw_ln in text.splitlines():
        ln = clean(raw_ln)
        ln = re.sub(r'^\[[^\]]+\]\s*(WARNING|ERROR)\s+', '', ln)
        match = COMPILER.search(ln)
        if match:
            rel_dir, filename, _line, msg = match.groups()
            app = _normalize_compile_target(rel_dir)
            msg = re.sub(r'\s+', ' ', msg).strip()
            category = classify_compile_failure(msg, ln)
            key = (app, filename, msg)
            if key not in seen:
                seen.add(key)
                found.append({'name': app, 'reason': f'{filename}: {msg}', 'kind': 'build', 'category': category})
            continue
        dep_match = DEP_ERR_RE.search(ln)
        if dep_match and default_app:
            msg = re.sub(r'\s+', ' ', dep_match.group(0)).strip()[:180]
            key = (default_app, 'dep', msg)
            if key not in seen:
                seen.add(key)
                found.append({'name': default_app, 'reason': msg, 'kind': 'build', 'category': 'Dependency Error'})
    return found


def compile_failures_from_archive(archive):
    found = []
    seen = set()
    for name in archive.namelist():
        if not name.endswith('build_log.txt'):
            continue
        app = _normalize_compile_target(name)
        try:
            log_text = archive.read(name).decode('utf-8', 'replace')
        except OSError:
            continue
        for item in compile_failures(log_text, default_app=app):
            key = (item['name'], item['reason'])
            if key not in seen:
                seen.add(key)
                found.append(item)
    return found


def _fallback_job_failure(job):
    name = job.get('name') or f"job-{job.get('id')}"
    is_build = is_build_job(job)
    kind = 'build' if is_build else 'target_test'
    reason = job.get('failure_reason') or 'job failed'
    category = 'Code / Build Error' if is_build else 'Env / Flash Error'
    return [{'name': name, 'reason': reason, 'kind': kind, 'category': category}]


def failures_from_job(project, job):
    infra_msg = is_prestart_or_infra_failure(job)
    if infra_msg:
        print(f"WARNING: Skip job {job.get('id')} ({job.get('name')}): {infra_msg}")
        return []
    job_id = job['id']
    archive = open_artifacts(project, job_id)
    if archive is not None:
        if is_build_job(job):
            compiled_arch = compile_failures_from_archive(archive)
            if compiled_arch:
                return compiled_arch
        else:
            rows = junit_fails(archive)
            if rows:
                return rows
    try:
        text = job_text(project, job_id, 'trace').decode('utf-8', 'replace')
    except (urllib.error.URLError, OSError):
        return _fallback_job_failure(job)
    infra_msg = is_prestart_or_infra_failure(job, text)
    if infra_msg:
        print(f"WARNING: Skip job {job.get('id')} ({job.get('name')}): {infra_msg}")
        return []
    cases = unity_fails(text)
    if cases:
        return [{'name': name, 'reason': msg, 'kind': 'target_test', 'category': 'Test Case Failure'} for name, msg in cases]
    rows = pytest_summary_fails(text)
    if rows:
        return rows
    compiled = compile_failures(text, default_app=job.get('name') or '')
    if compiled:
        return compiled
    return _fallback_job_failure(job)


def walk(project, pipeline_id, seen, bare_jobs):
    if pipeline_id in seen:
        return
    seen.add(pipeline_id)
    for job in paginate(f'/projects/{enc(project)}/pipelines/{pipeline_id}/jobs?include_retried=true'):
        if job.get('status') == 'failed':
            bare_jobs.append(job)
    for bridge in paginate(f'/projects/{enc(project)}/pipelines/{pipeline_id}/bridges?include_retried=true'):
        child = (bridge.get('downstream_pipeline') or {}).get('id')
        if child and bridge.get('status') != 'canceled':
            walk(project, child, seen, bare_jobs)


def fetch_pipeline_test_report_cases(project, pipeline_ids):
    cases = []
    for pid in pipeline_ids:
        report, _ = api('GET', f'/projects/{enc(project)}/pipelines/{pid}/test_report')
        for suite in (report or {}).get('test_suites') or []:
            for case in suite.get('test_cases') or []:
                if case.get('status') in ('failed', 'error'):
                    cases.append(case)
    return cases


def collect(days=7):
    end = datetime.now(TZ)
    start = end - timedelta(days=days)
    since = start.astimezone(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    found = []
    for label, project in PROJECTS:
        pipes = paginate(f'/projects/{enc(project)}/pipelines?updated_after={urllib.parse.quote(since)}')
        for pipe in pipes:
            if pipe.get('status') == 'canceled' or not is_landed(pipe.get('ref') or ''):
                continue
            visited_pids = set()
            jobs = []
            walk(project, pipe['id'], visited_pids, jobs)
            failed_jobs = [job for job in jobs if job.get('status') == 'failed']
            if not failed_jobs:
                continue
            seen_cases = set()

            def push(name, reason, kind, category=''):
                key = (kind, name, reason)
                if not name or key in seen_cases:
                    return
                seen_cases.add(key)
                if not category:
                    category = classify_compile_failure(reason) if kind == 'build' else classify_board_failure(reason)
                found.append({
                    'project': label,
                    'ref': pipe['ref'],
                    'kind': kind,
                    'category': category,
                    'name': name,
                    'reason': reason,
                    'url': pipe['web_url'],
                })

            for job in failed_jobs:
                for item in failures_from_job(project, job):
                    push(
                        item['name'],
                        item['reason'],
                        item.get('kind') or 'target_test',
                        item.get('category') or '',
                    )
            if not seen_cases:
                for case in fetch_pipeline_test_report_cases(project, visited_pids):
                    rsn = case_reason(case)
                    push(case.get('name') or '', rsn, 'target_test', classify_board_failure(rsn))
    return start.date().isoformat(), end.date().isoformat(), found


ROLE_TAGS_ORDER = (
    'ESP32',
    'ESP32S3',
    'ESP32P4',
    'ESP32S31',
    'ESP32C3',
    'ESP32S2',
    'esp32',
    'esp32s3',
    'esp32p4',
    'esp32s31',
    'esp32c3',
    'esp32s2',
    'ESP_GMF_COMPONENT',
    'multimedia_build',
    'adf_build',
    'AUDIO_LOOPBACK_ENV',
)


def collect_abnormal_runners():
    now = datetime.now(TZ)
    by_id = {}
    repos_by_id = defaultdict(list)
    online_descs = set()

    for label, project in PROJECTS:
        for r in paginate(f'/projects/{enc(project)}/runners'):
            rid = r['id']
            by_id[rid] = r
            if label not in repos_by_id[rid]:
                repos_by_id[rid].append(label)
            if r.get('status') == 'online' and not r.get('paused'):
                online_descs.add(r.get('description') or '')

    abnormal = []
    for rid, r in sorted(by_id.items(), key=lambda item: (item[1].get('description') or '', item[0])):
        status = r.get('status') or 'unknown'
        desc = r.get('description') or f'runner-{rid}'
        if status == 'online' and not r.get('paused'):
            continue
        # Skip obsolete runner registrations that have been replaced by an active runner with the same name
        if status == 'stale' and desc in online_descs:
            continue
        try:
            detail, _ = api('GET', f'/runners/{rid}')
        except SystemExit:
            detail = r
        tags = set((detail or {}).get('tag_list') or [])
        picked_tags = [t for t in ROLE_TAGS_ORDER if t in tags]
        contacted_raw = (detail or {}).get('contacted_at') or ''
        contacted_str = 'unknown'
        duration_str = 'paused' if (detail or {}).get('paused') or status == 'paused' else status
        if contacted_raw:
            try:
                dt = datetime.fromisoformat(contacted_raw.replace('Z', '+00:00')).astimezone(TZ)
                contacted_str = dt.strftime('%Y-%m-%d %H:%M')
                if status != 'paused' and not (detail or {}).get('paused'):
                    delta = now - dt
                    days = delta.days
                    hours = delta.seconds // 3600
                    duration_str = f'offline {days}d {hours}h' if days > 0 else f'offline {hours}h'
            except ValueError:
                contacted_str = contacted_raw[:16]
        abnormal.append({
            'id': rid,
            'description': desc,
            'status': 'paused' if (detail or {}).get('paused') else status,
            'repos': ', '.join(repos_by_id[rid]),
            'tags': ', '.join(picked_tags[:5]) or '-',
            'contacted': contacted_str,
            'duration': duration_str,
        })
    return abnormal


def week_marker(end):
    iso_year, iso_week, _ = datetime.strptime(end, '%Y-%m-%d').isocalendar()
    return f'<!-- weekly-ci-report:{iso_year}-W{iso_week:02d} -->'


def render(start, end, rows, abnormal_runners=None):
    lines = [
        week_marker(end),
        f'## {start} ~ {end}',
        '',
        'Counts landed pipelines only: GMF `main`, ADF `master` and `release/*` '
        '(pre-start infrastructure/network/Docker errors are excluded). '
        'Merge request pipelines are omitted.',
        '',
    ]
    sections = (
        ('build', 'Build Failures', 'Code / Build Error'),
        ('target_test', 'Target Test Failures', 'Test Case Failure'),
    )
    for kind, heading, default_cat in sections:
        picked = [row for row in rows if row.get('kind') == kind]
        lines.append(f'### {heading}')
        lines.append('')
        if not picked:
            lines.append('None')
            lines.append('')
            continue
        grouped = defaultdict(lambda: {'count': 0, 'urls': []})
        for row in picked:
            cat = row.get('category') or default_cat
            key = (row['project'], row['ref'], cat, row['name'], row['reason'])
            grouped[key]['count'] += 1
            if row['url'] not in grouped[key]['urls']:
                grouped[key]['urls'].append(row['url'])
        lines.append('| Count | Repo | Branch | Category | Target / Case | Reason | Pipelines |')
        lines.append('| --- | --- | --- | --- | --- | --- | --- |')
        order = sorted(grouped.items(), key=lambda item: (-item[1]['count'], item[0][0], item[0][2], item[0][3]))
        for (project, ref, cat, name, reason), info in order:
            links = ', '.join(f"[{url.rsplit('/', 1)[-1]}]({url})" for url in info['urls'])
            reason = reason.replace('|', '\\|')
            lines.append(f"| {info['count']} | {project} | `{ref}` | {cat} | `{name}` | {reason} | {links} |")
        lines.append('')

    if abnormal_runners is not None:
        lines.append('### Abnormal Runners (Offline / Paused)')
        lines.append('')
        if not abnormal_runners:
            lines.append('None (all online)')
            lines.append('')
        else:
            lines.append('| Runner | ID | Status | Repos | Role / Chip Tags | Last Contact | Note |')
            lines.append('| --- | --- | --- | --- | --- | --- | --- |')
            for r in abnormal_runners:
                lines.append(
                    f"| `{r['description']}` | {r['id']} | `{r['status']}` | {r['repos']} | "
                    f"`{r['tags']}` | {r['contacted']} | {r['duration']} |"
                )
            lines.append('')
    return '\n'.join(lines).rstrip() + '\n'


def issue_iid():
    try:
        issue, _ = api('GET', f'/projects/{enc(HOME_PROJECT)}/issues/{DEFAULT_ISSUE_IID}')
        if issue and issue.get('iid'):
            return issue['iid']
    except SystemExit:
        pass
    encoded = urllib.parse.quote(ISSUE_TITLE)
    issues = paginate(f'/projects/{enc(HOME_PROJECT)}/issues?state=opened&search={encoded}')
    for issue in issues:
        if issue.get('title') == ISSUE_TITLE:
            return issue['iid']
    created, _ = api('POST', f'/projects/{enc(HOME_PROJECT)}/issues', {
        'title': ISSUE_TITLE,
        'description': 'Weekly summary of failed test cases on GMF `main` and ADF `master` / `release/*`.',
    })
    return created['iid']


def publish(body, iid):
    body_lines = body.splitlines()
    marker = body_lines[0]
    header = body_lines[1] if len(body_lines) > 1 else marker
    notes = paginate(f'/projects/{enc(HOME_PROJECT)}/issues/{iid}/notes?sort=desc')
    for note in notes:
        note_body = note.get('body') or ''
        if not note.get('system') and (marker in note_body or note_body.startswith(header)):
            api('PUT', f"/projects/{enc(HOME_PROJECT)}/issues/{iid}/notes/{note['id']}", {'body': body})
            return 'updated'
    api('POST', f'/projects/{enc(HOME_PROJECT)}/issues/{iid}/notes', {'body': body})
    return 'created'


def main():
    start, end, rows = collect()
    abnormal_runners = collect_abnormal_runners()
    body = render(start, end, rows, abnormal_runners=abnormal_runners)
    iid = issue_iid()
    action = publish(body, iid)
    print(f'{action} issue !{iid}')
    print(f'{HOST}/{HOME_PROJECT}/-/issues/{iid}')


if __name__ == '__main__':
    main()
