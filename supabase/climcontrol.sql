-- =====================================================================
-- ClimControl · base de datos en Supabase
--
-- Cómo se usa: en Supabase → SQL Editor → New query → pegar TODO este
-- archivo → Run. Se puede volver a correr sin perder datos.
--
-- Tablas: perfiles, configuracion, aulas, dispositivos, lecturas, eventos.
-- Cada cuenta ve SOLO sus aulas (seguridad por filas, RLS).
-- La ESP32 no usa una cuenta: se identifica con el código y la clave de
-- su dispositivo y llama a la función dispositivo_reportar.
-- =====================================================================

create schema if not exists private;

-- ---------------------------------------------------------------------
-- 1) TABLAS
-- ---------------------------------------------------------------------

-- Datos de cada persona que se crea una cuenta
create table if not exists public.perfiles (
  id        uuid primary key references auth.users(id) on delete cascade,
  nombre    text not null default '' check (char_length(nombre) <= 60),
  creado_en timestamptz not null default now()
);

-- Configuración de cada cuenta: cuándo se prende cada equipo y cómo se estima el consumo
create table if not exists public.configuracion (
  usuario_id             uuid primary key references auth.users(id) on delete cascade,
  temp_ventilador        numeric(4,1) not null default 24  check (temp_ventilador between 15 and 40),
  temp_aire              numeric(4,1) not null default 27  check (temp_aire between 15 and 40),
  minutos_sin_movimiento int          not null default 5   check (minutos_sin_movimiento between 0 and 60),
  precio_kwh             numeric(10,2) not null default 180 check (precio_kwh between 0 and 100000),
  potencia_ventilador_w  int not null default 150  check (potencia_ventilador_w between 0 and 20000),
  potencia_luces_w       int not null default 180  check (potencia_luces_w between 0 and 20000),
  -- el aire gasta más cuanto más calor hace: tres rangos de temperatura del aula
  aire_limite_1          numeric(4,1) not null default 30 check (aire_limite_1 between 15 and 50),
  aire_limite_2          numeric(4,1) not null default 33 check (aire_limite_2 between 15 and 50),
  aire_w_bajo            int not null default 1100 check (aire_w_bajo  between 0 and 20000),
  aire_w_medio           int not null default 1500 check (aire_w_medio between 0 and 20000),
  aire_w_alto            int not null default 1900 check (aire_w_alto  between 0 and 20000),
  actualizado_en         timestamptz not null default now(),
  constraint aire_despues_del_ventilador check (temp_aire > temp_ventilador),
  constraint rangos_en_orden check (aire_limite_2 > aire_limite_1)
);

-- Aulas de cada cuenta
create table if not exists public.aulas (
  id         bigint generated always as identity primary key,
  usuario_id uuid not null default auth.uid() references auth.users(id) on delete cascade,
  nombre     text not null check (char_length(trim(nombre)) between 1 and 60),
  creada_en  timestamptz not null default now()
);
create unique index if not exists aulas_nombre_por_usuario on public.aulas (usuario_id, lower(nombre));

-- El dispositivo (ESP32) de cada aula. La clave se guarda cifrada (SHA-256).
create table if not exists public.dispositivos (
  id              bigint generated always as identity primary key,
  aula_id         bigint not null unique references public.aulas(id) on delete cascade,
  codigo          text not null unique,
  clave_hash      text not null,
  ultimo_contacto timestamptz,
  creado_en       timestamptz not null default now()
);

-- Lo que manda la ESP32 cada 10 segundos (sensores + estado de cada equipo)
create table if not exists public.lecturas (
  id           bigint generated always as identity primary key,
  aula_id      bigint not null references public.aulas(id) on delete cascade,
  fecha_hora   timestamptz not null default now(),
  temperatura  numeric(4,1),
  humedad      numeric(4,1),
  hay_personas boolean not null default false,
  ventilador   boolean not null default false,
  aire         boolean not null default false,
  luces        boolean not null default false
);
create index if not exists lecturas_aula_fecha on public.lecturas (aula_id, fecha_hora desc);

-- Un registro cada vez que un equipo se prende o se apaga (o cambia la presencia)
create table if not exists public.eventos (
  id         bigint generated always as identity primary key,
  aula_id    bigint not null references public.aulas(id) on delete cascade,
  fecha_hora timestamptz not null default now(),
  equipo     text not null check (equipo in ('ventilador', 'aire', 'luces', 'presencia')),
  encendido  boolean not null
);
create index if not exists eventos_aula_fecha on public.eventos (aula_id, fecha_hora desc);

-- ---------------------------------------------------------------------
-- 2) AL CREAR UNA CUENTA: su perfil y su configuración con los valores de fábrica
-- ---------------------------------------------------------------------
create or replace function private.nuevo_usuario() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  insert into public.perfiles (id, nombre)
  values (new.id, left(coalesce(nullif(trim(new.raw_user_meta_data ->> 'nombre'), ''), split_part(coalesce(new.email, ''), '@', 1)), 60))
  on conflict (id) do nothing;
  insert into public.configuracion (usuario_id) values (new.id) on conflict (usuario_id) do nothing;
  return new;
end $$;

drop trigger if exists climcontrol_nuevo_usuario on auth.users;
create trigger climcontrol_nuevo_usuario after insert on auth.users
  for each row execute function private.nuevo_usuario();

-- Cuentas que ya existían antes de correr este archivo
insert into public.perfiles (id, nombre)
  select id, left(split_part(coalesce(email, ''), '@', 1), 60) from auth.users on conflict (id) do nothing;
insert into public.configuracion (usuario_id) select id from auth.users on conflict (usuario_id) do nothing;

create or replace function private.marcar_actualizado() returns trigger
language plpgsql set search_path = '' as $$
begin new.actualizado_en := now(); return new; end $$;
drop trigger if exists configuracion_actualizada on public.configuracion;
create trigger configuracion_actualizada before update on public.configuracion
  for each row execute function private.marcar_actualizado();

-- ---------------------------------------------------------------------
-- 3) SEGURIDAD: cada cuenta ve y cambia solo lo suyo
-- ---------------------------------------------------------------------
alter table public.perfiles      enable row level security;
alter table public.configuracion enable row level security;
alter table public.aulas         enable row level security;
alter table public.dispositivos  enable row level security;
alter table public.lecturas      enable row level security;
alter table public.eventos       enable row level security;

drop policy if exists "ver mi perfil" on public.perfiles;
create policy "ver mi perfil" on public.perfiles for select to authenticated using (id = (select auth.uid()));
drop policy if exists "editar mi perfil" on public.perfiles;
create policy "editar mi perfil" on public.perfiles for update to authenticated
  using (id = (select auth.uid())) with check (id = (select auth.uid()));

drop policy if exists "ver mi configuracion" on public.configuracion;
create policy "ver mi configuracion" on public.configuracion for select to authenticated using (usuario_id = (select auth.uid()));
drop policy if exists "editar mi configuracion" on public.configuracion;
create policy "editar mi configuracion" on public.configuracion for update to authenticated
  using (usuario_id = (select auth.uid())) with check (usuario_id = (select auth.uid()));

drop policy if exists "ver mis aulas" on public.aulas;
create policy "ver mis aulas" on public.aulas for select to authenticated using (usuario_id = (select auth.uid()));
drop policy if exists "renombrar mis aulas" on public.aulas;
create policy "renombrar mis aulas" on public.aulas for update to authenticated
  using (usuario_id = (select auth.uid())) with check (usuario_id = (select auth.uid()));
drop policy if exists "borrar mis aulas" on public.aulas;
create policy "borrar mis aulas" on public.aulas for delete to authenticated using (usuario_id = (select auth.uid()));

drop policy if exists "ver mis dispositivos" on public.dispositivos;
create policy "ver mis dispositivos" on public.dispositivos for select to authenticated
  using (exists (select 1 from public.aulas a where a.id = aula_id and a.usuario_id = (select auth.uid())));
drop policy if exists "ver mis lecturas" on public.lecturas;
create policy "ver mis lecturas" on public.lecturas for select to authenticated
  using (exists (select 1 from public.aulas a where a.id = aula_id and a.usuario_id = (select auth.uid())));
drop policy if exists "ver mis eventos" on public.eventos;
create policy "ver mis eventos" on public.eventos for select to authenticated
  using (exists (select 1 from public.aulas a where a.id = aula_id and a.usuario_id = (select auth.uid())));

-- Permisos por columna: nadie puede cambiar a quién pertenece algo, y la clave cifrada no se lee
revoke all on public.perfiles, public.configuracion, public.aulas, public.dispositivos, public.lecturas, public.eventos from anon, authenticated;
grant select on public.perfiles to authenticated;
grant update (nombre) on public.perfiles to authenticated;
grant select on public.configuracion to authenticated;
grant update (temp_ventilador, temp_aire, minutos_sin_movimiento, precio_kwh, potencia_ventilador_w, potencia_luces_w,
              aire_limite_1, aire_limite_2, aire_w_bajo, aire_w_medio, aire_w_alto) on public.configuracion to authenticated;
grant select, delete on public.aulas to authenticated;          -- las aulas se crean con crear_aula()
grant update (nombre) on public.aulas to authenticated;
grant select (id, aula_id, codigo, ultimo_contacto, creado_en) on public.dispositivos to authenticated;
grant select on public.lecturas, public.eventos to authenticated;   -- solo la ESP32 escribe, con dispositivo_reportar()

-- ---------------------------------------------------------------------
-- 4) FUNCIONES
-- ---------------------------------------------------------------------
create or replace function private.hash_clave(p text) returns text
language sql immutable set search_path = '' as $$ select encode(sha256(convert_to(coalesce(p, ''), 'UTF8')), 'hex') $$;

create or replace function private.clave_nueva() returns text
language sql volatile set search_path = '' as $$ select replace(gen_random_uuid()::text, '-', '') $$;

create or replace function private.codigo_nuevo() returns text
language plpgsql volatile set search_path = '' as $$
declare c text;
begin
  loop
    c := 'CC-' || upper(substr(replace(gen_random_uuid()::text, '-', ''), 1, 6));
    exit when not exists (select 1 from public.dispositivos d where d.codigo = c);
  end loop;
  return c;
end $$;

-- Crear un aula con su dispositivo. Devuelve el código y la clave para cargar en la ESP32
-- (la clave se muestra UNA sola vez: en la base queda cifrada).
create or replace function public.crear_aula(p_nombre text) returns json
language plpgsql security definer set search_path = '' as $$
declare
  v_uid    uuid := auth.uid();
  v_nombre text := trim(coalesce(p_nombre, ''));
  v_aula   bigint;
  v_codigo text := private.codigo_nuevo();
  v_clave  text := private.clave_nueva();
begin
  if v_uid is null then raise exception 'SIN_SESION' using errcode = '28000'; end if;
  if char_length(v_nombre) not between 1 and 60 then raise exception 'NOMBRE_INVALIDO' using errcode = '22023'; end if;
  if exists (select 1 from public.aulas a where a.usuario_id = v_uid and lower(a.nombre) = lower(v_nombre)) then
    raise exception 'NOMBRE_REPETIDO' using errcode = '23505';
  end if;
  if (select count(*) from public.aulas a where a.usuario_id = v_uid) >= 50 then
    raise exception 'DEMASIADAS_AULAS' using errcode = '54000';
  end if;
  insert into public.aulas (usuario_id, nombre) values (v_uid, v_nombre) returning id into v_aula;
  insert into public.dispositivos (aula_id, codigo, clave_hash) values (v_aula, v_codigo, private.hash_clave(v_clave));
  return json_build_object('aula_id', v_aula, 'codigo', v_codigo, 'clave', v_clave);
end $$;

-- Generar una clave nueva para el dispositivo de un aula (la anterior deja de funcionar)
create or replace function public.nueva_clave_dispositivo(p_aula bigint) returns json
language plpgsql security definer set search_path = '' as $$
declare v_codigo text; v_clave text := private.clave_nueva();
begin
  if not exists (select 1 from public.aulas a where a.id = p_aula and a.usuario_id = auth.uid()) then
    raise exception 'AULA_INEXISTENTE' using errcode = 'P0002';
  end if;
  insert into public.dispositivos as d (aula_id, codigo, clave_hash)
  values (p_aula, private.codigo_nuevo(), private.hash_clave(v_clave))
  on conflict (aula_id) do update set clave_hash = excluded.clave_hash
  returning d.codigo into v_codigo;
  return json_build_object('codigo', v_codigo, 'clave', v_clave);
end $$;

-- La ESP32 llama a esta función cada 10 segundos (y apenas cambia un equipo).
-- Guarda la lectura, registra los eventos y le devuelve las temperaturas de la configuración.
create or replace function public.dispositivo_reportar(
  p_codigo text, p_clave text,
  p_temperatura numeric default null, p_humedad numeric default null,
  p_hay_personas boolean default false, p_ventilador boolean default false,
  p_aire boolean default false, p_luces boolean default false)
returns json
language plpgsql security definer set search_path = '' as $$
declare
  v_disp bigint; v_aula bigint; v_usuario uuid;
  v_hay_previa boolean;
  v_fecha timestamptz; v_vent boolean; v_aire boolean; v_luces boolean; v_gente boolean;
  v_cambio boolean;
  c record;
begin
  select d.id, d.aula_id, a.usuario_id into v_disp, v_aula, v_usuario
    from public.dispositivos d join public.aulas a on a.id = d.aula_id
   where d.codigo = upper(trim(coalesce(p_codigo, ''))) and d.clave_hash = private.hash_clave(p_clave);
  if v_disp is null then raise exception 'DISPOSITIVO_INVALIDO' using errcode = '28000'; end if;

  p_hay_personas := coalesce(p_hay_personas, false);
  p_ventilador := coalesce(p_ventilador, false);
  p_aire := coalesce(p_aire, false);
  p_luces := coalesce(p_luces, false);
  if p_temperatura < -40 or p_temperatura > 85 then p_temperatura := null; end if;   -- el sensor falló
  if p_humedad < 0 or p_humedad > 100 then p_humedad := null; end if;

  select l.fecha_hora, l.ventilador, l.aire, l.luces, l.hay_personas
    into v_fecha, v_vent, v_aire, v_luces, v_gente
    from public.lecturas l where l.aula_id = v_aula order by l.fecha_hora desc limit 1;
  v_hay_previa := found;

  -- Eventos: uno por cada cosa que cambió desde la lectura anterior
  insert into public.eventos (aula_id, equipo, encendido)
  select v_aula, e.equipo, e.ahora
    from (values ('ventilador', p_ventilador, coalesce(v_vent, false)),
                 ('aire',       p_aire,       coalesce(v_aire, false)),
                 ('luces',      p_luces,      coalesce(v_luces, false)),
                 ('presencia',  p_hay_personas, coalesce(v_gente, false))) as e(equipo, ahora, antes)
   where e.ahora <> e.antes;
  v_cambio := found;

  -- Lectura: como mucho una cada 5 segundos, salvo que algo haya cambiado
  if not v_hay_previa or v_cambio or now() - v_fecha >= interval '5 seconds' then
    insert into public.lecturas (aula_id, temperatura, humedad, hay_personas, ventilador, aire, luces)
    values (v_aula, round(p_temperatura, 1), round(p_humedad, 1), p_hay_personas, p_ventilador, p_aire, p_luces);
  end if;

  update public.dispositivos set ultimo_contacto = now() where id = v_disp;

  select cf.temp_ventilador, cf.temp_aire, cf.minutos_sin_movimiento into c
    from public.configuracion cf where cf.usuario_id = v_usuario;
  return json_build_object(
    'temp_ventilador',        coalesce(c.temp_ventilador, 24),
    'temp_aire',              coalesce(c.temp_aire, 27),
    'minutos_sin_movimiento', coalesce(c.minutos_sin_movimiento, 5));
end $$;

-- Consumo estimado por aula y por día. Se calcula con las lecturas: cada lectura cuenta
-- el tiempo hasta la siguiente (como mucho 30 segundos, por si el dispositivo se desconectó)
-- y la última de cada aula cuenta 10 segundos.
create or replace function public.consumo_por_dia(p_desde timestamptz, p_hasta timestamptz)
returns table (aula_id bigint, dia date,
               horas_ventilador numeric, horas_aire numeric, horas_luces numeric,
               kwh_ventilador numeric, kwh_aire numeric, kwh_luces numeric)
language sql stable security invoker set search_path = '' as $$
  with c as (
    select * from public.configuracion where usuario_id = (select auth.uid())
  ), con_siguiente as (
    select l.aula_id, l.fecha_hora, l.temperatura, l.ventilador, l.aire, l.luces,
           lead(l.fecha_hora) over (partition by l.aula_id order by l.fecha_hora) as siguiente
      from public.lecturas l
     where l.fecha_hora >= p_desde and l.fecha_hora < p_hasta
  ), tramos as (
    select s.*,
           case when s.siguiente is null
                then greatest(least(extract(epoch from least(now(), p_hasta) - s.fecha_hora), 10), 0)
                else least(extract(epoch from s.siguiente - s.fecha_hora), 30) end as seg
      from con_siguiente s
  )
  select t.aula_id,
         (t.fecha_hora at time zone 'America/Argentina/Cordoba')::date as dia,
         round(sum(case when t.ventilador then t.seg else 0 end) / 3600.0, 3),
         round(sum(case when t.aire then t.seg else 0 end) / 3600.0, 3),
         round(sum(case when t.luces then t.seg else 0 end) / 3600.0, 3),
         round(sum(case when t.ventilador then t.seg else 0 end) * c.potencia_ventilador_w / 3600000.0, 4),
         round(sum(case when t.aire then t.seg * case when t.temperatura >= c.aire_limite_2 then c.aire_w_alto
                                                      when t.temperatura >= c.aire_limite_1 then c.aire_w_medio
                                                      else c.aire_w_bajo end
                        else 0 end) / 3600000.0, 4),
         round(sum(case when t.luces then t.seg else 0 end) * c.potencia_luces_w / 3600000.0, 4)
    from tramos t cross join c
   group by t.aula_id, 2, c.potencia_ventilador_w, c.potencia_luces_w
   order by 2, 1
$$;

-- Temperatura, humedad y presencia agrupadas cada p_paso_seg segundos (para el gráfico)
create or replace function public.lecturas_grafico(p_aula bigint, p_desde timestamptz, p_paso_seg int default 60)
returns table (t timestamptz, temperatura numeric, humedad numeric, hay_personas boolean)
language sql stable security invoker set search_path = '' as $$
  select date_bin(make_interval(secs => greatest(p_paso_seg, 10)), l.fecha_hora, timestamptz '2000-01-01 00:00:00+00'),
         round(avg(l.temperatura), 1), round(avg(l.humedad), 1), bool_or(l.hay_personas)
    from public.lecturas l
   where l.aula_id = p_aula and l.fecha_hora >= p_desde
   group by 1
   order by 1
$$;

revoke execute on all functions in schema private from public, anon, authenticated;
revoke execute on function public.crear_aula(text), public.nueva_clave_dispositivo(bigint),
  public.consumo_por_dia(timestamptz, timestamptz), public.lecturas_grafico(bigint, timestamptz, int) from public, anon;
grant execute on function public.crear_aula(text), public.nueva_clave_dispositivo(bigint),
  public.consumo_por_dia(timestamptz, timestamptz), public.lecturas_grafico(bigint, timestamptz, int) to authenticated;
revoke execute on function public.dispositivo_reportar(text, text, numeric, numeric, boolean, boolean, boolean, boolean) from public;
grant execute on function public.dispositivo_reportar(text, text, numeric, numeric, boolean, boolean, boolean, boolean) to anon, authenticated;

-- ---------------------------------------------------------------------
-- 5) VISTAS: el estado actual de cada aula y las consultas del trabajo
-- (security_invoker: cada cuenta ve solo sus filas)
-- ---------------------------------------------------------------------
create or replace view public.estado_aulas with (security_invoker = true) as
select a.id as aula_id, a.nombre, d.codigo, d.ultimo_contacto,
       l.fecha_hora, l.temperatura, l.humedad, l.hay_personas, l.ventilador, l.aire, l.luces
  from public.aulas a
  left join public.dispositivos d on d.aula_id = a.id
  left join lateral (select x.fecha_hora, x.temperatura, x.humedad, x.hay_personas, x.ventilador, x.aire, x.luces
                       from public.lecturas x where x.aula_id = a.id
                      order by x.fecha_hora desc limit 1) l on true;

-- Consulta 1: promedio de temperatura y humedad de cada aula
create or replace view public.consulta_promedios_por_aula with (security_invoker = true) as
select a.nombre as aula, count(l.id) as lecturas,
       round(avg(l.temperatura), 1) as temperatura_promedio, round(avg(l.humedad), 1) as humedad_promedio
  from public.aulas a
  left join public.lecturas l on l.aula_id = a.id
 group by a.id, a.nombre
 order by a.nombre;

-- Consulta 2: momentos con gente y calor (desde la temperatura del ventilador)
create or replace view public.consulta_calor_con_gente with (security_invoker = true) as
select a.nombre as aula, l.fecha_hora, l.temperatura, l.humedad
  from public.lecturas l
  join public.aulas a on a.id = l.aula_id
  join public.configuracion c on c.usuario_id = a.usuario_id
 where l.hay_personas and l.temperatura >= c.temp_ventilador
 order by l.fecha_hora desc
 limit 50;

-- Consulta 3: cuántas veces se prendió cada equipo en cada aula
create or replace view public.consulta_encendidos_por_equipo with (security_invoker = true) as
select a.nombre as aula, e.equipo, count(*) as veces_que_se_prendio, max(e.fecha_hora) as ultima_vez
  from public.eventos e
  join public.aulas a on a.id = e.aula_id
 where e.encendido and e.equipo <> 'presencia'
 group by a.nombre, e.equipo
 order by a.nombre, e.equipo;

-- Consulta 4: cada aula con su responsable y su dispositivo
create or replace view public.consulta_aulas_y_dispositivos with (security_invoker = true) as
select a.nombre as aula, p.nombre as responsable, d.codigo as dispositivo, d.ultimo_contacto,
       coalesce(d.ultimo_contacto > now() - interval '1 minute', false) as en_linea
  from public.aulas a
  join public.perfiles p on p.id = a.usuario_id
  left join public.dispositivos d on d.aula_id = a.id
 order by a.nombre;

-- Consulta 5: las últimas 10 lecturas
create or replace view public.consulta_ultimas_lecturas with (security_invoker = true) as
select a.nombre as aula, l.fecha_hora, l.temperatura, l.humedad, l.hay_personas, l.ventilador, l.aire, l.luces
  from public.lecturas l
  join public.aulas a on a.id = l.aula_id
 order by l.fecha_hora desc
 limit 10;

revoke all on public.estado_aulas, public.consulta_promedios_por_aula, public.consulta_calor_con_gente,
  public.consulta_encendidos_por_equipo, public.consulta_aulas_y_dispositivos, public.consulta_ultimas_lecturas from anon, authenticated;
grant select on public.estado_aulas, public.consulta_promedios_por_aula, public.consulta_calor_con_gente,
  public.consulta_encendidos_por_equipo, public.consulta_aulas_y_dispositivos, public.consulta_ultimas_lecturas to authenticated;
