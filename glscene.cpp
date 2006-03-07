/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools projectmay not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "glscene.h"

#include "glcontroller.h"
#include "glnode.h"
#include "glnodeswitch.h"
#include "gllight.h"
#include "glmesh.h"
#include "glparticles.h"
#include "gltex.h"


Scene::Scene()
{
	texturing = true;
	blending = true;
	highlight = true;
	drawHidden = false;
	currentNode = 0;
	animate = true;
	
	time = distance = 0.0;
	sceneBoundsValid = timeBoundsValid = false;
}

Scene::~Scene()
{
	qDeleteAll( textures ); textures.clear();
}

void Scene::clear( bool flushTextures )
{
	nodes.clear();
	properties.clear();
	lights.clear();
	roots.clear();
	
	if ( flushTextures )
		foreach ( GLTex * tex, textures )
			tex->invalidate();

	sceneBoundsValid = timeBoundsValid = false;
}

void Scene::update( const NifModel * nif, const QModelIndex & index )
{
	if ( ! nif )
		return;
	
	if ( index.isValid() )
	{
		QModelIndex block = nif->getBlock( index );
		if ( ! block.isValid() )
			return;
		
		foreach ( Property * prop, properties.list() )
			prop->update( nif, block );
		
		foreach ( Node * node, nodes.list() )
			node->update( nif, block );
		
		foreach ( GLTex * tex, textures )
			if ( tex->iSource == block || tex->iPixelData == block )
				tex->invalidate();
	}
	else
	{
		properties.validate();
		nodes.validate();
		lights.validate();
		
		foreach ( Node * n, nodes.list() )
			n->update( nif, QModelIndex() );
		foreach ( Property * p, properties.list() )
			p->update( nif, QModelIndex() );
		
		roots.clear();
		foreach ( int link, nif->getRootLinks() )
		{
			QModelIndex iBlock = nif->getBlock( link );
			if ( iBlock.isValid() )
			{
				Node * node = getNode( nif, iBlock );
				if ( node ) roots.add( node );
			}
		}
	}
	
	timeBoundsValid = false;
}

void Scene::make( NifModel * nif, bool flushTextures )
{
	clear( flushTextures );
	if ( ! nif ) return;
	update( nif, QModelIndex() );
}

Node * Scene::getNode( const NifModel * nif, const QModelIndex & iNode )
{
	if ( ! ( nif && iNode.isValid() ) )
		return 0;
	
	Node * node = nodes.get( iNode );
	if ( node ) return node;
	
	if ( nif->inherits( iNode, "AParentNode" ) )
	{
		if ( nif->itemName( iNode ) == "NiLODNode" )
			node = new LODNode( this, iNode );
		else if ( nif->itemName( iNode ) == "NiBillboardNode" )
			node = new BillboardNode( this, iNode );
		else
			node = new Node( this, iNode );
	}
	else if ( nif->itemName( iNode ) == "NiTriShape" || nif->itemName( iNode ) == "NiTriStrips" )
	{
		node = new Mesh( this, iNode );
	}
	else if ( nif->inherits( iNode, "ALight" ) )
	{
		node = new Light( this, iNode );
		lights.add( node );
	}
	else if ( nif->inherits( iNode, "AParticleNode" ) )
	{
		node = new Particles( this, iNode );
	}

	if ( node )
	{
		node->update( nif, iNode );
		nodes.add( node );
	}

	return node;
}

Property * Scene::getProperty( const NifModel * nif, const QModelIndex & iProperty )
{
	Property * prop = properties.get( iProperty );
	if ( prop ) return prop;
	properties.add( prop = Property::create( this, nif, iProperty ) );
	return prop;
}

bool compareLights( const QPair< Light *, float > l1, const QPair< Light *, float > l2 )
{
	return l1.second < l2.second;
}

void Scene::transform( const Transform & trans, float time )
{
	view = trans;
	this->time = time;
	
	worldTrans.clear();
	viewTrans.clear();
	
	foreach ( Property * prop, properties.list() )
		prop->transform();
	
	foreach ( Node * node, roots.list() )
		node->transform();
	foreach ( Node * node, roots.list() )
		node->transformShapes();

	sceneBoundsValid = false;

	QList<GLTex*> rem;
	foreach ( GLTex * tex, textures )
	{
		if ( ! tex->isValid() )
			rem.append( tex );
	}
	foreach ( GLTex * tex, rem )
	{
		textures.removeAll( tex );
		delete tex;
	}
}

void Scene::drawShapes()
{	
	glEnable( GL_CULL_FACE );
	
	NodeList draw2nd;

	foreach ( Node * node, roots.list() )
		node->drawShapes( &draw2nd );
	
	draw2nd.sort();

	foreach ( Node * node, draw2nd.list() )
		node->drawShapes();
}

void Scene::drawNodes()
{
	foreach ( Node * node, roots.list() )
		node->draw( 0 );
}

void Scene::setupLights( Node * node )
{
	glEnable( GL_LIGHTING );
	
	if ( lights.list().isEmpty() || ! lighting )
	{
		static const GLfloat L0position[4] = { 0.0, 0.0, 1.0, 0.0f };
		static const GLfloat L0ambient[4] = { 0.4f, 0.4f, 0.4f, 1.0f };
		static const GLfloat L0diffuse[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
		static const GLfloat L0specular[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glLightfv( GL_LIGHT0, GL_POSITION, L0position );
		glLightfv( GL_LIGHT0, GL_AMBIENT, L0ambient );
		glLightfv( GL_LIGHT0, GL_DIFFUSE, L0diffuse );
		glLightfv( GL_LIGHT0, GL_SPECULAR, L0specular );
		glEnable( GL_LIGHT0 );
		int n = 1; while ( n < 8 ) Light::off( n++ );
	}
	else if ( lights.list().count() <= 8 )
	{
		int c = 0;
		foreach ( Node * n, lights.list() )
			static_cast<Light*>( n )->on( c++ );
		while ( c < 8 )
			Light::off( c++ );
	}
	else
	{
		QList< QPair< Light *, float > > slights;
		foreach ( Node * n, lights.list() )
		{
			Light * l = static_cast<Light*>( n );
			slights.append( QPair< Light *, float >( l, ( l->worldTrans().translation - node->worldTrans().translation ).length() ) );
		}
		qStableSort( slights.begin(), slights.end(), compareLights );
		for ( int n = 0; n < 8; n++ )
			slights[n].first->on( n++ );
	}
}

void Scene::updateBoundaries() const
{
	if ( ! nodes.list().isEmpty() )
	{
		nodes.list().first()->boundaries( bMin, bMax );
		foreach ( Node * node, nodes.list() )
		{
			Vector3 mn, mx;
			node->boundaries( mn, mx );
			bMin.boundMin( mn );
			bMax.boundMax( mx );
		}
		for ( int c = 0; c < 3; c++ )
		{
			bRadius[c] = ( bMax[c] - bMin[c] ) / 2;
			bCenter[c] = bMin[c] + bRadius[c];
		}
	}
	else
		bMin = bMax = bCenter = bRadius = Vector3();
	
	sceneBoundsValid = true;
}

Vector3 Scene::boundMin() const
{
	if ( ! sceneBoundsValid )
		updateBoundaries();
	return bMin;
}

Vector3 Scene::boundMax() const
{
	if ( ! sceneBoundsValid )
		updateBoundaries();
	return bMax;
}

Vector3 Scene::boundCenter() const
{
	if ( ! sceneBoundsValid )
		updateBoundaries();
	return bCenter;
}

Vector3 Scene::boundExtend() const
{
	if ( ! sceneBoundsValid )
		updateBoundaries();
	return bRadius;
}

float Scene::boundRadius() const
{
	if ( ! sceneBoundsValid )
		updateBoundaries();
	return qMax( bRadius[0], qMax( bRadius[1], bRadius[2] ) );
}

void Scene::updateTimeBounds() const
{
	if ( ! nodes.list().isEmpty() )
	{
		tMin = +1000000000; tMax = -1000000000;
		foreach ( Node * node, nodes.list() )
			node->timeBounds( tMin, tMax );
		foreach ( Property * prop, properties.list() )
			prop->timeBounds( tMin, tMax );
	}
	else
		tMin = tMax = 0;
	timeBoundsValid = true;
}

float Scene::timeMin() const
{
	if ( ! timeBoundsValid )
		updateTimeBounds();
	return tMin;
}

float Scene::timeMax() const
{
	if ( ! timeBoundsValid )
		updateTimeBounds();
	return tMax;
}