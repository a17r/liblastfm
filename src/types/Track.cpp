/*
   Copyright 2009 Last.fm Ltd. 
      - Primarily authored by Max Howell, Jono Cole and Doug Mansell

   This file is part of liblastfm.

   liblastfm is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   liblastfm is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with liblastfm.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "Track.h"
#include "User.h"
#include "../core/UrlBuilder.h"
#include "../core/XmlQuery.h"
#include "../ws/ws.h"
#include <QFileInfo>
#include <QStringList>


lastfm::TrackData::TrackData()
             : trackNumber( 0 ),
               duration( 0 ),
               source( Track::Unknown ),
               rating( 0 ),
               fpid( -1 ),
               loved( false ),
               null( false )
{}


void
lastfm::TrackData::onLoveFinished()
{
    XmlQuery lfm = static_cast<QNetworkReply*>(sender())->readAll();
    if ( lfm.attribute( "status" ) == "ok")
        loved = true;
    emit loveToggled( loved );
}

void
lastfm::TrackData::onUnloveFinished()
{
    XmlQuery lfm = static_cast<QNetworkReply*>(sender())->readAll();
    if ( lfm.attribute( "status" ) == "ok")
        loved = false;
    emit loveToggled( loved );
}


lastfm::Track::Track()
    :AbstractType()
{
    d = new TrackData;
    d->null = true;
}

lastfm::Track::Track( const QDomElement& e )
    :AbstractType()
{
    d = new TrackData;

    if (e.isNull()) { d->null = true; return; }
    
    d->artist = e.namedItem( "artist" ).toElement().text();
    d->album =  e.namedItem( "album" ).toElement().text();
    d->title = e.namedItem( "track" ).toElement().text();
    d->trackNumber = 0;
    d->duration = e.namedItem( "duration" ).toElement().text().toInt();
    d->url = e.namedItem( "url" ).toElement().text();
    d->rating = e.namedItem( "rating" ).toElement().text().toUInt();
    d->source = e.namedItem( "source" ).toElement().text().toInt(); //defaults to 0, or lastfm::Track::Unknown
    d->time = QDateTime::fromTime_t( e.namedItem( "timestamp" ).toElement().text().toUInt() );
    d->loved = e.namedItem( "loved" ).toElement().text() != "0";

    for (QDomElement image(e.firstChildElement("image")) ; !image.isNull() ; image = e.nextSiblingElement("image"))
    {
        d->m_images[static_cast<lastfm::ImageSize>(image.attribute("size").toInt())] = image.text();
    }

    QDomNodeList nodes = e.namedItem( "extras" ).childNodes();
    for (int i = 0; i < nodes.count(); ++i)
    {
        QDomNode n = nodes.at(i);
        QString key = n.nodeName();
        d->extras[key] = n.toElement().text();
    }
}


QDomElement
lastfm::Track::toDomElement( QDomDocument& xml ) const
{
    QDomElement item = xml.createElement( "track" );
    
    #define makeElement( tagname, getter ) { \
        QString v = getter; \
        if (!v.isEmpty()) \
        { \
            QDomElement e = xml.createElement( tagname ); \
            e.appendChild( xml.createTextNode( v ) ); \
            item.appendChild( e ); \
        } \
    }

    makeElement( "artist", d->artist );
    makeElement( "album", d->album );
    makeElement( "track", d->title );
    makeElement( "duration", QString::number( d->duration ) );
    makeElement( "timestamp", QString::number( d->time.toTime_t() ) );
    makeElement( "url", d->url.toString() );
    makeElement( "source", QString::number( d->source ) );
    makeElement( "rating", QString::number(d->rating) );
    makeElement( "fpId", QString::number(d->fpid) );
    makeElement( "mbId", mbid() );
    makeElement( "loved", QString::number( isLoved() ) );

    // put the images urls in the dom
    QMapIterator<lastfm::ImageSize, QUrl> imageIter( d->m_images );
    while (imageIter.hasNext()) {
        QDomElement e = xml.createElement( "image" );
        e.appendChild( xml.createTextNode( imageIter.next().value().toString() ) );
        e.setAttribute( "size", imageIter.key() );
        item.appendChild( e );
    }

    // add the extras to the dom
    QDomElement extras = xml.createElement( "extras" );
    QMapIterator<QString, QString> extrasIter( d->extras );
    while (extrasIter.hasNext()) {
        QDomElement e = xml.createElement( extrasIter.next().key() );
        e.appendChild( xml.createTextNode( extrasIter.value() ) );
        extras.appendChild( e );
    }
    item.appendChild( extras );

    return item;
}


QUrl
lastfm::Track::imageUrl( lastfm::ImageSize size, bool square ) const
{
    if( !square ) return d->m_images.value( size );

    QUrl url = d->m_images.value( size );
    QRegExp re( "/serve/(\\d*)s?/" );
    return QUrl( url.toString().replace( re, "/serve/\\1s/" ));
}


QString
lastfm::Track::toString( const QChar& separator ) const
{
    if ( d->artist.isEmpty() )
    {
        if ( d->title.isEmpty() )
            return QFileInfo( d->url.path() ).fileName();
        else
            return d->title;
    }

    if ( d->title.isEmpty() )
        return d->artist;

    return d->artist + ' ' + separator + ' ' + d->title;
}


QString //static
lastfm::Track::durationString( int const duration )
{
    QTime t = QTime().addSecs( duration );
    if (duration < 60*60)
        return t.toString( "m:ss" );
    else
        return t.toString( "hh:mm:ss" );
}


QNetworkReply*
lastfm::Track::share( const QStringList& recipients, const QString& message, bool isPublic ) const
{
    QMap<QString, QString> map = params("share");
    map["recipient"] = recipients.join(",");
    map["public"] = isPublic ? "1" : "0";
    if (message.size()) map["message"] = message;
    return ws::post(map);
}


void
lastfm::MutableTrack::setFromLfm( const XmlQuery& lfm )
{
    QString imageUrl = lfm["image size=small"].text();
    if ( !imageUrl.isEmpty() ) d->m_images[lastfm::Small] = imageUrl;
    imageUrl = lfm["image size=medium"].text();
    if ( !imageUrl.isEmpty() ) d->m_images[lastfm::Medium] = imageUrl;
    imageUrl = lfm["image size=large"].text();
    if ( !imageUrl.isEmpty() ) d->m_images[lastfm::Large] = imageUrl;
    imageUrl = lfm["image size=extralarge"].text();
    if ( !imageUrl.isEmpty() ) d->m_images[lastfm::ExtraLarge] = imageUrl;
    imageUrl = lfm["image size=mega"].text();
    if ( !imageUrl.isEmpty() ) d->m_images[lastfm::Mega] = imageUrl;

    d->loved = lfm["userloved"].text() == "1";
}


void
lastfm::MutableTrack::love()
{
    QNetworkReply* reply = ws::post(params("love"));
    QObject::connect( reply, SIGNAL(finished()), d.data(), SLOT(onLoveFinished()));
}


void
lastfm::MutableTrack::unlove()
{
    QNetworkReply* reply = ws::post(params("unlove"));
    QObject::connect( reply, SIGNAL(finished()), d.data(), SLOT(onUnloveFinished()));
}


QNetworkReply*
lastfm::MutableTrack::ban()
{
    d->extras["rating"] = "B";
    return ws::post(params("ban"));
}


QMap<QString, QString>
lastfm::Track::params( const QString& method, bool use_mbid ) const
{
    QMap<QString, QString> map;
    map["method"] = "Track."+method;
    if (d->mbid.size() && use_mbid)
        map["mbid"] = d->mbid;
    else {
        map["artist"] = d->artist;
        map["track"] = d->title;
    }
    return map;
}


QNetworkReply*
lastfm::Track::getTopTags() const
{
    return ws::get( params("getTopTags", true) );
}


QNetworkReply*
lastfm::Track::getTopFans() const
{
    return ws::get( params("getTopFans", true) );
}


QNetworkReply*
lastfm::Track::getTags() const
{
    return ws::get( params("getTags", true) );
}

QNetworkReply*
lastfm::Track::getInfo(const QString& user, const QString& sk) const
{
    QMap<QString, QString> map = params("getInfo", true);
    if (!user.isEmpty()) map["username"] = user;
    if (!sk.isEmpty()) map["sk"] = sk;
    return ws::get( map );
}


QNetworkReply*
lastfm::Track::addTags( const QStringList& tags ) const
{
    if (tags.isEmpty())
        return 0;
    QMap<QString, QString> map = params("addTags");
    map["tags"] = tags.join( QChar(',') );
    return ws::post(map);
}


QNetworkReply*
lastfm::Track::removeTag( const QString& tag ) const
{
    if (tag.isEmpty())
        return 0;
    QMap<QString, QString> map = params( "removeTag" );
    map["tags"] = tag;
    return ws::post(map);
}


QNetworkReply*
lastfm::Track::scrobble()
{
    QMap<QString, QString> map;
    map["method"] = "Track.scrobble";
    map["duration"] = QString::number( d->duration );
    map["timestamp"] = QString::number( d->time.toTime_t() );
    map["track"] = d->title;
    map["context"] = extra("playerName");
    if ( !d->album.isEmpty() ) map["album"] = d->album;
    map["artist"] = d->artist;
    if ( !d->mbid.isEmpty() ) map["mbid"] = d->mbid;
    return ws::post(map);
}

QNetworkReply*
lastfm::Track::scrobbleBatch(const QList<lastfm::Track>& tracks)
{
    QMap<QString, QString> map;
    map["method"] = "Track.scrobbleBatch";

    for ( int i(0) ; i < tracks.count() ; ++i )
    {
        map["duration[" + QString::number(i) + "]"] = QString::number( tracks[i].duration() );
        map["timestamp[" + QString::number(i)  + "]"] = QString::number( tracks[i].timestamp().toTime_t() );
        map["track[" + QString::number(i)  + "]"] = tracks[i].title();
        map["context[" + QString::number(i)  + "]"] = tracks[i].extra("playerName");
        if ( !tracks[i].album().isNull() ) map["album[" + QString::number(i)  + "]"] = tracks[i].album();
        map["artist[" + QString::number(i) + "]"] = tracks[i].artist();
        if ( !tracks[i].mbid().isNull() ) map["mbid[" + QString::number(i)  + "]"] = tracks[i].mbid();
    }

    return ws::post(map);
}


QUrl
lastfm::Track::www() const
{
    return UrlBuilder( "music" ).slash( d->artist ).slash( album().isNull() ? QString("_") : album()).slash( d->title ).url();
}


bool
lastfm::Track::isMp3() const
{
    //FIXME really we should check the file header?
    return d->url.scheme() == "file" &&
           d->url.path().endsWith( ".mp3", Qt::CaseInsensitive );
}


lastfm::Track
lastfm::Track::clone() const
{
    Track copy( *this );
    copy.d.detach();
    return copy;
}
